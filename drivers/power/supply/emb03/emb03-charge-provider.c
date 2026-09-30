// SPDX-License-Identifier: GPL-2.0-only
/* EMB03 built-in SY6915 provider. Normal probe does not program registers.
 * Explicit batteryless opt-in inhibits charge before publishing the provider.
 * Explicitly no generic register-write/debug interface in minimal bring-up.
 * Handles own independent references; device links provide ordering only.
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pm.h>
#include "emb03-charge-provider.h"

static DEFINE_MUTEX(provider_lock);
static struct emb03_charge *provider;

static void emb03_charge_release(struct kref *ref)
{
	struct emb03_charge *charge = container_of(ref, struct emb03_charge, refs);

	kfree(charge);
}

static void emb03_supplier_put(void *arg)
{
	struct emb03_charge *charge = arg;

	kref_put(&charge->refs, emb03_charge_release);
}

struct emb03_charge *emb03_charge_acquire(struct device *consumer)
{
	struct emb03_charge *charge;
	struct device *supplier;
	struct device_link *link;
	int ret;

	mutex_lock(&provider_lock);
	charge = provider;
	if (!charge) {
		mutex_unlock(&provider_lock);
		return ERR_PTR(-EPROBE_DEFER);
	}
	kref_get(&charge->refs);
	supplier = get_device(&charge->client->dev);
	mutex_unlock(&provider_lock);
	/* A concurrent remove may already have started. The reference, not
	 * the link, protects charge during this window. detach serializes I2C.
	 */
	link = device_link_add(consumer, supplier, DL_FLAG_AUTOREMOVE_CONSUMER);
	put_device(supplier);
	if (!link) {
		emb03_supplier_put(charge);
		return ERR_PTR(-EPROBE_DEFER);
	}
	mutex_lock(&charge->lock);
	ret = charge->dead ? -EPROBE_DEFER : 0;
	mutex_unlock(&charge->lock);
	if (ret) {
		emb03_supplier_put(charge);
		return ERR_PTR(ret);
	}
	ret = devm_add_action_or_reset(consumer, emb03_supplier_put, charge);
	if (ret)
		return ERR_PTR(ret);
	return charge;
}

bool emb03_charge_is_batteryless(const struct emb03_charge *charge)
{
	/* Immutable for the lifetime of this referenced handle, even after detach. */
	return charge->batteryless;
}

#define EMB03_REASSERT_MS 5000

static void emb03_sy_reassert(struct work_struct *work)
{
	struct emb03_charge *charge = container_of(to_delayed_work(work),
					struct emb03_charge, reassert_work);

	emb03_charge_inhibit(charge);
	mutex_lock(&charge->lock);
	if (!charge->reassert_stopped && !charge->dead)
		schedule_delayed_work(&charge->reassert_work,
				      msecs_to_jiffies(EMB03_REASSERT_MS));
	mutex_unlock(&charge->lock);
}

static void emb03_sy_stop_work(struct emb03_charge *charge)
{
	mutex_lock(&charge->lock);
	charge->reassert_stopped = true;
	mutex_unlock(&charge->lock);
	cancel_delayed_work_sync(&charge->reassert_work);
}

static void emb03_sy_start_work(struct emb03_charge *charge)
{
	mutex_lock(&charge->lock);
	if (charge->batteryless && !charge->dead) {
		charge->reassert_stopped = false;
		schedule_delayed_work(&charge->reassert_work,
				      msecs_to_jiffies(EMB03_REASSERT_MS));
	}
	mutex_unlock(&charge->lock);
}

static int emb03_sy_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct emb03_charge *charge;
	int ret;

	if (!i2c_check_functionality(client->adapter,
		I2C_FUNC_SMBUS_READ_I2C_BLOCK | I2C_FUNC_SMBUS_WRITE_I2C_BLOCK))
		return -EOPNOTSUPP;
	charge = kzalloc(sizeof(*charge), GFP_KERNEL);
	if (!charge)
		return -ENOMEM;
	emb03_charge_init(charge, client);
	INIT_DELAYED_WORK(&charge->reassert_work, emb03_sy_reassert);
	charge->batteryless = of_property_read_bool(client->dev.of_node,
						    "factory48,batteryless") ||
			      of_property_read_bool(client->dev.of_node,
						    "polyhex,batteryless");
	i2c_set_clientdata(client, charge);
	mutex_lock(&provider_lock);
	if (provider) {
		mutex_unlock(&provider_lock);
		i2c_set_clientdata(client, NULL);
		emb03_supplier_put(charge);
		return -EBUSY;
	}
	/* No CW presence, probe success or sample validity is needed to stop. */
	if (charge->batteryless) {
		ret = emb03_charge_inhibit(charge);
		if (ret) {
			mutex_unlock(&provider_lock);
			i2c_set_clientdata(client, NULL);
			emb03_supplier_put(charge);
			return ret;
		}
		dev_warn(&client->dev,
			 "candidate batteryless mode: synthetic battery ABI\n");
	}
	emb03_sy_start_work(charge);
	provider = charge;
	mutex_unlock(&provider_lock);
	return 0;
}

static int emb03_sy_remove(struct i2c_client *client)
{
	struct emb03_charge *charge = i2c_get_clientdata(client);

	mutex_lock(&provider_lock);
	if (provider == charge)
		provider = NULL;
	/* Lock order: provider_lock -> charge->lock. No transaction takes
	 * provider_lock. Existing references survive removal, but cannot I2C.
	 */
	emb03_sy_stop_work(charge);
	/* Best effort final stop; do not restore charging on removal. */
	if (charge->batteryless)
		emb03_charge_inhibit(charge);
	emb03_charge_detach(charge);
	mutex_unlock(&provider_lock);
	i2c_set_clientdata(client, NULL);
	emb03_supplier_put(charge);
	return 0;
}

static void emb03_sy_shutdown(struct i2c_client *client)
{
	struct emb03_charge *charge = i2c_get_clientdata(client);

	emb03_sy_stop_work(charge);
	if (charge->batteryless)
		emb03_charge_inhibit(charge);
}

#ifdef CONFIG_PM_SLEEP
static int emb03_sy_suspend(struct device *dev)
{
	struct emb03_charge *charge = i2c_get_clientdata(to_i2c_client(dev));
	int ret = 0;

	emb03_sy_stop_work(charge);
	if (charge->batteryless)
		ret = emb03_charge_inhibit(charge);
	/* A failed suspend must not leave a live provider without its worker. */
	if (ret)
		emb03_sy_start_work(charge);
	return ret;
}

static int emb03_sy_resume(struct device *dev)
{
	struct emb03_charge *charge = i2c_get_clientdata(to_i2c_client(dev));
	int ret = 0;

	if (charge->batteryless)
		ret = emb03_charge_inhibit(charge);
	/* Retry while active even when synchronous resume verification failed. */
	emb03_sy_start_work(charge);
	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(emb03_sy_pm_ops, emb03_sy_suspend, emb03_sy_resume);

static const struct i2c_device_id emb03_sy_ids[] = {
	{ "sy6915", 0 }, { }
};
MODULE_DEVICE_TABLE(i2c, emb03_sy_ids);
static const struct of_device_id emb03_sy_of[] = {
	{ .compatible = "sy6915" }, { }
};
MODULE_DEVICE_TABLE(of, emb03_sy_of);
static struct i2c_driver emb03_sy_driver = {
	.driver = {
		.name = "sy6915",
		.of_match_table = emb03_sy_of,
		.suppress_bind_attrs = true,
		.pm = &emb03_sy_pm_ops,
	},
	.probe = emb03_sy_probe,
	.remove = emb03_sy_remove,
	.shutdown = emb03_sy_shutdown,
	.id_table = emb03_sy_ids,
};
module_i2c_driver(emb03_sy_driver);
MODULE_LICENSE("GPL");
