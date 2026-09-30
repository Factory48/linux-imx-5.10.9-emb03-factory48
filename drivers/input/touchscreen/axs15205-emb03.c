// SPDX-License-Identifier: GPL-2.0-only
/* EMB03 AXS15205: reconstructed from board disassembly, not UGREEN wire format.
 * No reset GPIO ownership, firmware download, raw-register or update interface.
 */
#include <linux/delay.h>
#include <linux/emb03-panel.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

/* At most 10 wake attempts (3 STOP-delimited transfers each), 50 ms apart.
 * Adapter timeout/IRQ drain can add latency: this is not a wall-clock bound.
 */
#define AXS_WAKE_ATTEMPTS 10
#define AXS_WAKE_DELAY_MS 50
#include "axs15205-frame.h"

struct emb03_axs {
	struct i2c_client *client;
	struct input_dev *input;
	/* Lifecycle callers only: the IRQ thread MUST NOT acquire this mutex.
	 * disable_irq() drains all bus/report activity before lifecycle I/O.
	 */
	struct mutex lifecycle;
	struct emb03_panel_listener panel;
	struct delayed_work wake_work;
	unsigned int wake_attempts;
	int irq;
	unsigned int active;
	bool panel_ready, pm_suspended, stopped, removing;
};

/* Caller is either the oneshot IRQ thread or lifecycle with IRQ drained.
 * Separate STOP-delimited send/receive, three attempts, exact lengths only.
 */
static int axs_transfer(struct emb03_axs *ts, u8 cmd, u8 *buf, int len)
{
	int ret = -EIO, attempt;

	for (attempt = 0; attempt < 3; attempt++) {
		ret = i2c_master_send(ts->client, &cmd, 1);
		if (ret == 1) {
			ret = i2c_master_recv(ts->client, buf, len);
			if (ret == len)
				return 0;
		}
		if (ret >= 0)
			ret = -EIO;
	}
	return ret;
}

static void axs_release(struct emb03_axs *ts)
{
	int i;

	for (i = 0; i < EMB03_AXS_MAX_POINTS; i++) {
		input_mt_slot(ts->input, i);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
	}
	input_report_key(ts->input, BTN_TOUCH, 0);
	input_sync(ts->input);
	ts->active = 0;
}

static void axs_report(struct emb03_axs *ts, struct emb03_axs_point *p,
		       int n, u8 count)
{
	unsigned int active = 0;
	int i;

	/* Original zero-entry branch is an error, NOT an all-up frame. */
	if (n <= 0)
		return;
	for (i = 0; i < n; i++, p++) {
		input_mt_slot(ts->input, p->id);
		if (p->event == 0 || p->event == 2) {
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, true);
			input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR,
					 p->area ? p->area : 9);
			input_report_abs(ts->input, ABS_MT_POSITION_X, p->x);
			input_report_abs(ts->input, ABS_MT_POSITION_Y, p->y);
			active |= BIT(p->id);
			ts->active |= BIT(p->id);
		} else {
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
			ts->active &= ~BIT(p->id);
		}
	}
	for (i = 0; i < EMB03_AXS_MAX_POINTS; i++) {
		if ((ts->active ^ active) & BIT(i)) {
			input_mt_slot(ts->input, i);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
		}
	}
	ts->active = active;
	input_report_key(ts->input, BTN_TOUCH, !!(active && count));
	input_sync(ts->input);
}

static irqreturn_t axs_irq(int irq, void *data)
{
	struct emb03_axs *ts = data;
	struct emb03_axs_point points[EMB03_AXS_MAX_POINTS];
	u8 buf[EMB03_AXS_BUFFER_SIZE];
	int n;

	/* request_threaded_irq() enables this 5.10 IRQ before probe can drain
	 * it. Initial stopped=true prevents bus access in that brief window.
	 */
	if (READ_ONCE(ts->stopped))
		return IRQ_HANDLED;
	memset(buf, 0xff, sizeof(buf));
	buf[0] = 0;
	n = axs_transfer(ts, 0, buf, EMB03_AXS_WIRE_SIZE);
	if (n)
		return IRQ_HANDLED;
	n = emb03_axs_decode(buf, sizeof(buf), points);
	axs_report(ts, points, n, buf[2] & 15);
	return IRQ_HANDLED;
}

/* All helpers below run with lifecycle held. The worker never enters DRM
 * or the panel provider, so a synchronous OFF callback can drain I/O here.
 */
static bool axs_can_wake(struct emb03_axs *ts)
{
	return ts->panel_ready && !ts->pm_suspended && !ts->removing;
}

static void axs_stop(struct emb03_axs *ts)
{
	/* Non-sync cancellation is safe under lifecycle; stale work rechecks
	 * the gates. NEVER cancel_delayed_work_sync() while holding this lock.
	 */
	cancel_delayed_work(&ts->wake_work);
	if (!ts->stopped) {
		disable_irq(ts->irq);
		WRITE_ONCE(ts->stopped, true);
		axs_release(ts);
	}
}

static void axs_start(struct emb03_axs *ts)
{
	if (!axs_can_wake(ts) || !ts->stopped)
		return;
	ts->wake_attempts = 0;
	mod_delayed_work(system_wq, &ts->wake_work,
			 msecs_to_jiffies(AXS_WAKE_DELAY_MS));
}

static void axs_wake_work(struct work_struct *work)
{
	struct emb03_axs *ts = container_of(to_delayed_work(work),
					  struct emb03_axs, wake_work);
	u8 reply[2] = { 0 };
	int ret;

	mutex_lock(&ts->lifecycle);
	if (!axs_can_wake(ts) || !ts->stopped ||
	    ts->wake_attempts >= AXS_WAKE_ATTEMPTS)
		goto out;
	ts->wake_attempts++;
	ret = axs_transfer(ts, 0xa3, reply, sizeof(reply));
	if (!ret) {
		axs_release(ts);
		WRITE_ONCE(ts->stopped, false);
		enable_irq(ts->irq);
		dev_dbg(&ts->client->dev, "touch ready after %u wake attempts\n",
			ts->wake_attempts);
	} else if (ts->wake_attempts < AXS_WAKE_ATTEMPTS) {
		mod_delayed_work(system_wq, &ts->wake_work,
				 msecs_to_jiffies(AXS_WAKE_DELAY_MS));
	} else {
		dev_err(&ts->client->dev,
			"touch wake failed after %u attempts: %d; IRQ disabled\n",
			ts->wake_attempts, ret);
	}
out:
	mutex_unlock(&ts->lifecycle);
}

static void axs_panel_event(struct emb03_panel_listener *listener, bool ready)
{
	struct emb03_axs *ts = container_of(listener, struct emb03_axs, panel);

	mutex_lock(&ts->lifecycle);
	if (!ts->removing) {
		bool changed = ts->panel_ready != ready;

		ts->panel_ready = ready;
		if (!ready)
			axs_stop(ts);
		else if (changed)
			axs_start(ts);
	}
	mutex_unlock(&ts->lifecycle);
}

static int __maybe_unused axs_suspend(struct device *dev)
{
	struct emb03_axs *ts = i2c_get_clientdata(to_i2c_client(dev));

	mutex_lock(&ts->lifecycle);
	ts->pm_suspended = true;
	axs_stop(ts);
	mutex_unlock(&ts->lifecycle);
	return 0;
}

static int __maybe_unused axs_resume(struct device *dev)
{
	struct emb03_axs *ts = i2c_get_clientdata(to_i2c_client(dev));

	/* Resume can follow an aborted suspend with the display still OFF.
	 * Do not access I2C or wait for DRM here. Either event ordering works.
	 */
	mutex_lock(&ts->lifecycle);
	ts->pm_suspended = false;
	axs_start(ts);
	mutex_unlock(&ts->lifecycle);
	return 0;
}

static SIMPLE_DEV_PM_OPS(axs_pm, axs_suspend, axs_resume);

static int axs_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct emb03_axs *ts;
	struct input_dev *input;
	int gpio, ret;

	if (client->addr != 0x38 || (client->flags & I2C_CLIENT_TEN))
		return -ENODEV;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;
	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;
	ts->client = client;
	mutex_init(&ts->lifecycle);
	INIT_DELAYED_WORK(&ts->wake_work, axs_wake_work);
	ts->stopped = true;
	ts->panel.notify = axs_panel_event;
	i2c_set_clientdata(client, ts);
	gpio = of_get_named_gpio(client->dev.of_node, "touch,irq-gpio", 0);
	if (gpio < 0)
		return gpio;
	ret = devm_gpio_request_one(&client->dev, gpio, GPIOF_IN, "axs-irq");
	if (ret)
		return ret;
	ts->irq = gpio_to_irq(gpio);
	if (ts->irq < 0)
		return ts->irq;
	/* Original reset routine is empty: do not even request reset GPIO. */
	msleep(100);
	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;
	ts->input = input;
	input->name = "axs_ts";
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;
	input_set_capability(input, EV_KEY, BTN_TOUCH);
	input_set_capability(input, EV_KEY, BTN_TOOL_FINGER);
	/* Keep original ordering: do not synthesize legacy ABS_X/ABS_Y. */
	ret = input_mt_init_slots(input, EMB03_AXS_MAX_POINTS, INPUT_MT_DIRECT);
	if (ret)
		return ret;
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, 720, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, 1280, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	ret = input_register_device(input);
	if (ret)
		return ret;
	/* Everything used by IRQ is initialized before it can run. */
	ret = request_threaded_irq(ts->irq, NULL, axs_irq,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "axs_ts", ts);
	if (ret)
		return ret;
	disable_irq(ts->irq);
	/* No lifecycle lock here: registration replays state synchronously. */
	ret = emb03_panel_register_listener(&ts->panel);
	if (ret) {
		free_irq(ts->irq, ts);
		axs_release(ts);
		return ret;
	}
	return 0;
}

static int axs_remove(struct i2c_client *client)
{
	struct emb03_axs *ts = i2c_get_clientdata(client);

	mutex_lock(&ts->lifecycle);
	ts->removing = true;
	axs_stop(ts);
	mutex_unlock(&ts->lifecycle);
	/* Neither operation may run with lifecycle held. After unregister and
	 * cancel_sync no callback/worker can touch devres-managed storage.
	 */
	emb03_panel_unregister_listener(&ts->panel);
	cancel_delayed_work_sync(&ts->wake_work);
	free_irq(ts->irq, ts);
	return 0;
}

static const struct of_device_id axs_of_match[] = {
	{ .compatible = "axs15205,axs15205_touch" },
	{ }
};
MODULE_DEVICE_TABLE(of, axs_of_match);

static const struct i2c_device_id axs_ids[] = {
	{ "axs_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, axs_ids);

static struct i2c_driver axs_driver = {
	.driver = {
		.name = "axs15205-emb03",
		.of_match_table = axs_of_match,
		.pm = &axs_pm,
	},
	.probe = axs_probe,
	.remove = axs_remove,
	.id_table = axs_ids,
};
module_i2c_driver(axs_driver);

MODULE_DESCRIPTION("EMB03 AXS15205 I2C touchscreen (no firmware update)");
MODULE_LICENSE("GPL");
