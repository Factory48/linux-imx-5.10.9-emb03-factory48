// SPDX-License-Identifier: GPL-2.0-or-later
/* EMB03 reconstruction. See evidence/reverse/mxc_bt_*.asm and
 * evidence/gpio-and-bluetooth-review.md. Not yet compiled or device-tested.
 * Raw GPIO levels intentionally match the vendor driver, including DT flags.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/rfkill.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>

struct emb03_bt {
	int power, wake_bt, wake_host;
	struct rfkill *rfkill;
	struct notifier_block pm;
	struct mutex lock;
	bool suspending;
};

static int emb03_bt_block(void *arg, bool blocked)
{
	struct emb03_bt *bt = arg;

	mutex_lock(&bt->lock);
	/* Original hardware policy keeps power on when blocked/suspended.
	 * Android unblocking resets the controller using raw low/high levels.
	 */
	if (!bt->suspending && !blocked) {
		gpio_set_value_cansleep(bt->power, 0);
		msleep(500);
		gpio_set_value_cansleep(bt->power, 1);
	}
	mutex_unlock(&bt->lock);
	return 0;
}

static const struct rfkill_ops emb03_bt_ops = {
	.set_block = emb03_bt_block,
};

static int emb03_bt_pm(struct notifier_block *nb, unsigned long event, void *arg)
{
	struct emb03_bt *bt = container_of(nb, struct emb03_bt, pm);

	mutex_lock(&bt->lock);
	if (event == PM_SUSPEND_PREPARE)
		bt->suspending = true;
	else if (event == PM_POST_SUSPEND)
		bt->suspending = false;
	mutex_unlock(&bt->lock);
	return NOTIFY_DONE;
}

static int emb03_bt_gpio(struct device *dev, const char *property,
			unsigned long flags, const char *label)
{
	int gpio = of_get_named_gpio(dev->of_node, property, 0);
	int ret;

	if (gpio < 0)
		return gpio;
	if (!gpio_is_valid(gpio))
		return -EINVAL;
	ret = devm_gpio_request_one(dev, gpio, flags, label);
	return ret ? ret : gpio;
}

static int emb03_bt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct emb03_bt *bt;
	int ret;

	bt = devm_kzalloc(dev, sizeof(*bt), GFP_KERNEL);
	if (!bt)
		return -ENOMEM;
	mutex_init(&bt->lock);
	bt->power = emb03_bt_gpio(dev, "bt-power-gpios",
				 GPIOF_OUT_INIT_HIGH, "BT power enable");
	if (bt->power < 0)
		return bt->power;
	bt->wake_bt = emb03_bt_gpio(dev, "wake-bt-gpios",
				   GPIOF_OUT_INIT_HIGH, "Wake BT Gpio");
	if (bt->wake_bt < 0)
		return bt->wake_bt;
	bt->wake_host = emb03_bt_gpio(dev, "wake-host-gpios",
				     GPIOF_IN, "BT wake host");
	if (bt->wake_host < 0)
		return bt->wake_host;

	bt->rfkill = rfkill_alloc("mx8_bt", dev, RFKILL_TYPE_BLUETOOTH,
				 &emb03_bt_ops, bt);
	if (!bt->rfkill)
		return -ENOMEM;
	bt->pm.notifier_call = emb03_bt_pm;
	ret = register_pm_notifier(&bt->pm);
	if (ret)
		goto destroy;
	ret = rfkill_register(bt->rfkill);
	if (ret)
		goto unregister_pm;
	platform_set_drvdata(pdev, bt);
	return 0;
unregister_pm:
	unregister_pm_notifier(&bt->pm);
destroy:
	rfkill_destroy(bt->rfkill);
	return ret;
}

static int emb03_bt_remove(struct platform_device *pdev)
{
	struct emb03_bt *bt = platform_get_drvdata(pdev);

	rfkill_unregister(bt->rfkill);
	unregister_pm_notifier(&bt->pm);
	rfkill_destroy(bt->rfkill);
	return 0;
}

static const struct of_device_id emb03_bt_match[] = {
	{ .compatible = "fsl,mxc_bt_rfkill" },
	{ }
};
MODULE_DEVICE_TABLE(of, emb03_bt_match);

static struct platform_driver emb03_bt_driver = {
	.probe = emb03_bt_probe,
	.remove = emb03_bt_remove,
	.driver = {
		.name = "mxc_bt_rfkill",
		.of_match_table = emb03_bt_match,
	},
};
module_platform_driver(emb03_bt_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EMB03 Bluetooth power and rfkill reconstruction");
