// SPDX-License-Identifier: GPL-2.0-only
/*
 * LEDs driver for GPIOs
 *
 * Copyright (C) 2007 8D Technologies inc.
 * Raphael Assenat <raph@8d.com>
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
 */
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/gpio/driver.h>
#include <linux/slab.h>

struct gpio_led_data {
	struct led_classdev cdev;
	struct gpio_desc *gpiod;
	u8 can_sleep;
	u8 blinking;
	gpio_blink_set_t platform_gpio_blink_set;
};

static inline struct gpio_led_data *
			cdev_to_gpio_led_data(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct gpio_led_data, cdev);
}

static void gpio_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	struct gpio_led_data *led_dat = cdev_to_gpio_led_data(led_cdev);
	int level;

	if (value == LED_OFF)
		level = 0;
	else
		level = 1;

	if (led_dat->blinking) {
		led_dat->platform_gpio_blink_set(led_dat->gpiod, level,
						 NULL, NULL);
		led_dat->blinking = 0;
	} else {
		if (led_dat->can_sleep)
			gpiod_set_value_cansleep(led_dat->gpiod, level);
		else
			gpiod_set_value(led_dat->gpiod, level);
	}
}

static int gpio_led_set_blocking(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	gpio_led_set(led_cdev, value);
	return 0;
}

static int gpio_blink_set(struct led_classdev *led_cdev,
	unsigned long *delay_on, unsigned long *delay_off)
{
	struct gpio_led_data *led_dat = cdev_to_gpio_led_data(led_cdev);

	led_dat->blinking = 1;
	return led_dat->platform_gpio_blink_set(led_dat->gpiod, GPIO_LED_BLINK,
						delay_on, delay_off);
}

static int create_gpio_led(const struct gpio_led *template,
	struct gpio_led_data *led_dat, struct device *parent,
	struct fwnode_handle *fwnode, gpio_blink_set_t blink_set)
{
	struct led_init_data init_data = {};
	int ret, state;

	led_dat->cdev.default_trigger = template->default_trigger;
	led_dat->can_sleep = gpiod_cansleep(led_dat->gpiod);
	if (!led_dat->can_sleep)
		led_dat->cdev.brightness_set = gpio_led_set;
	else
		led_dat->cdev.brightness_set_blocking = gpio_led_set_blocking;
	led_dat->blinking = 0;
	if (blink_set) {
		led_dat->platform_gpio_blink_set = blink_set;
		led_dat->cdev.blink_set = gpio_blink_set;
	}
	if (template->default_state == LEDS_GPIO_DEFSTATE_KEEP) {
		state = gpiod_get_value_cansleep(led_dat->gpiod);
		if (state < 0)
			return state;
	} else {
		state = (template->default_state == LEDS_GPIO_DEFSTATE_ON);
	}
	led_dat->cdev.brightness = state ? LED_FULL : LED_OFF;
	if (!template->retain_state_suspended)
		led_dat->cdev.flags |= LED_CORE_SUSPENDRESUME;
	if (template->panic_indicator)
		led_dat->cdev.flags |= LED_PANIC_INDICATOR;
	if (template->retain_state_shutdown)
		led_dat->cdev.flags |= LED_RETAIN_AT_SHUTDOWN;

	ret = gpiod_direction_output(led_dat->gpiod, state);
	if (ret < 0)
		return ret;

	if (template->name) {
		led_dat->cdev.name = template->name;
		ret = devm_led_classdev_register(parent, &led_dat->cdev);
	} else {
		init_data.fwnode = fwnode;
		ret = devm_led_classdev_register_ext(parent, &led_dat->cdev,
						     &init_data);
	}

	return ret;
}

struct gpio_leds_priv {
	int num_leds;
	bool shared_rail;
	struct gpio_led_data leds[];
};

/*
 * EMB03 (CONFIG_LEDS_GPIO_EMB03_SHARED_RAIL): the gpio-leds children on this
 * board are power switches (4G power/key/wake, boost, 5V, TF-card rail, ...),
 * not LEDs, and all of them must exist and stay controllable.
 *
 * The factory DT names GPIO2_IO19 in both /gpio-leds/NVCC_SD2 and the
 * /regulator-usdhc2 fixed regulator (enable-active-high, regulator-always-on).
 * On 5.10.9 gpio-leds requested the line first and the regulator shared it
 * (fixed regulators request with GPIOD_FLAGS_BIT_NONEXCLUSIVE). On 5.10.72 the
 * regulator requests it first, the exclusive request returns -EBUSY and the
 * whole gpio-leds probe failed, unwinding every switch to off.
 *
 * For exactly that DT pair, gpio-leds reuses the regulator's descriptor:
 * - the regulator device is locked while it is verified to be bound to
 *   reg-fixed-voltage and to hold the request, and while a managed device link
 *   (gpio-leds consumer) is added, so it cannot be mid-unbind and must unbind
 *   gpio-leds before it can release the line;
 * - every LED is unregistered from ->remove() and on probe failure, while that
 *   link still blocks the regulator, instead of later from devres (after
 *   driver core has already dropped the link).
 * No new request is taken; the owner's label stays. Any other busy GPIO still
 * fails the probe, and the code is inert unless the option is enabled.
 */
static struct gpio_desc *emb03_share_rail_gpio(struct device *dev,
					       struct fwnode_handle *child)
{
	struct gpio_desc *ret = ERR_PTR(-EBUSY);
	struct device_node *np = to_of_node(child);
	struct platform_device *reg_pdev;
	struct device_node *reg_np;
	enum of_gpio_flags flags;
	struct gpio_desc *desc;
	struct device *reg_dev;
	struct gpio_chip *gc;
	const char *label;
	const char *owner;
	int gpio;

	if (!IS_ENABLED(CONFIG_LEDS_GPIO_EMB03_SHARED_RAIL) || !np ||
	    of_property_read_string(np, "label", &label) ||
	    strcmp(label, "NVCC_SD2"))
		return ret;
	gpio = of_get_named_gpio_flags(np, "gpios", 0, &flags);
	if (!gpio_is_valid(gpio) || (flags & OF_GPIO_ACTIVE_LOW))
		return ret;

	reg_np = of_find_node_by_path("/regulator-usdhc2");
	if (!reg_np)
		return ret;
	if (!of_device_is_compatible(reg_np, "regulator-fixed") ||
	    !of_property_read_bool(reg_np, "enable-active-high") ||
	    !of_property_read_bool(reg_np, "regulator-always-on") ||
	    of_get_named_gpio(reg_np, "gpio", 0) != gpio) {
		of_node_put(reg_np);
		return ret;
	}
	reg_pdev = of_find_device_by_node(reg_np);
	of_node_put(reg_np);
	if (!reg_pdev)
		return ret;
	reg_dev = &reg_pdev->dev;

	/*
	 * The regulator releases the line only from its own probe failure or
	 * unbind, both under its device lock; unbind first marks it
	 * DL_DEV_UNBINDING under that lock. Bound + locked: the request and its
	 * label are stable, and the link below makes its unbind wait for ours.
	 */
	device_lock(reg_dev);
	if (reg_dev->links.status != DL_DEV_DRIVER_BOUND || !reg_dev->driver ||
	    strcmp(reg_dev->driver->name, "reg-fixed-voltage"))
		goto unlock;
	desc = gpio_to_desc(gpio);
	gc = desc ? gpiod_to_chip(desc) : NULL;
	if (!gc)
		goto unlock;
	owner = gpiochip_is_requested(gc, gpio - gc->base);
	if (!owner || strcmp(owner, dev_name(reg_dev)) ||
	    gpiod_is_active_low(desc))
		goto unlock;
	if (!device_link_add(dev, reg_dev, DL_FLAG_AUTOREMOVE_CONSUMER))
		goto unlock;

	dev_info(dev, "%s: sharing GPIO %d with %s\n", label, gpio,
		 dev_name(reg_dev));
	ret = desc;
unlock:
	device_unlock(reg_dev);
	put_device(reg_dev);
	return ret;
}

/*
 * Unregister every LED now, while the device link to the rail owner is still
 * active, so the final brightness writes cannot race the owner's unbind.
 */
static void gpio_leds_unregister_shared(struct device *dev,
					struct gpio_leds_priv *priv)
{
	while (priv->num_leds)
		devm_led_classdev_unregister(dev,
				&priv->leds[--priv->num_leds].cdev);
}

static struct gpio_leds_priv *gpio_leds_create(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *child;
	struct gpio_leds_priv *priv;
	int count, ret;

	count = device_get_child_node_count(dev);
	if (!count)
		return ERR_PTR(-ENODEV);

	priv = devm_kzalloc(dev, struct_size(priv, leds, count), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	device_for_each_child_node(dev, child) {
		struct gpio_led_data *led_dat = &priv->leds[priv->num_leds];
		struct gpio_led led = {};
		const char *state = NULL;
		bool shared = false;

		/*
		 * Acquire gpiod from DT with uninitialized label, which
		 * will be updated after LED class device is registered,
		 * Only then the final LED name is known.
		 */
		led.gpiod = devm_fwnode_get_gpiod_from_child(dev, NULL, child,
							     GPIOD_ASIS,
							     NULL);
		if (PTR_ERR_OR_ZERO(led.gpiod) == -EBUSY) {
			led.gpiod = emb03_share_rail_gpio(dev, child);
			shared = !IS_ERR(led.gpiod);
			priv->shared_rail |= shared;
		}
		if (IS_ERR(led.gpiod)) {
			ret = PTR_ERR(led.gpiod);
			goto err;
		}

		led_dat->gpiod = led.gpiod;

		if (!fwnode_property_read_string(child, "default-state",
						 &state)) {
			if (!strcmp(state, "keep"))
				led.default_state = LEDS_GPIO_DEFSTATE_KEEP;
			else if (!strcmp(state, "on"))
				led.default_state = LEDS_GPIO_DEFSTATE_ON;
			else
				led.default_state = LEDS_GPIO_DEFSTATE_OFF;
		}

		if (fwnode_property_present(child, "retain-state-suspended"))
			led.retain_state_suspended = 1;
		if (fwnode_property_present(child, "retain-state-shutdown"))
			led.retain_state_shutdown = 1;
		if (fwnode_property_present(child, "panic-indicator"))
			led.panic_indicator = 1;

		ret = create_gpio_led(&led, led_dat, dev, child, NULL);
		if (ret < 0)
			goto err;
		/*
		 * Set gpiod label to match the corresponding LED name. A shared
		 * line keeps its owner's label (the owner holds the request).
		 */
		if (!shared)
			gpiod_set_consumer_name(led_dat->gpiod,
						led_dat->cdev.dev->kobj.name);
		priv->num_leds++;
	}

	return priv;

err:
	fwnode_handle_put(child);
	/* Driver core drops the device link before devres runs. */
	if (priv->shared_rail)
		gpio_leds_unregister_shared(dev, priv);
	return ERR_PTR(ret);
}

static const struct of_device_id of_gpio_leds_match[] = {
	{ .compatible = "gpio-leds", },
	{},
};

MODULE_DEVICE_TABLE(of, of_gpio_leds_match);

static struct gpio_desc *gpio_led_get_gpiod(struct device *dev, int idx,
					    const struct gpio_led *template)
{
	struct gpio_desc *gpiod;
	unsigned long flags = GPIOF_OUT_INIT_LOW;
	int ret;

	/*
	 * This means the LED does not come from the device tree
	 * or ACPI, so let's try just getting it by index from the
	 * device, this will hit the board file, if any and get
	 * the GPIO from there.
	 */
	gpiod = devm_gpiod_get_index(dev, NULL, idx, GPIOD_OUT_LOW);
	if (!IS_ERR(gpiod)) {
		gpiod_set_consumer_name(gpiod, template->name);
		return gpiod;
	}
	if (PTR_ERR(gpiod) != -ENOENT)
		return gpiod;

	/*
	 * This is the legacy code path for platform code that
	 * still uses GPIO numbers. Ultimately we would like to get
	 * rid of this block completely.
	 */

	/* skip leds that aren't available */
	if (!gpio_is_valid(template->gpio))
		return ERR_PTR(-ENOENT);

	if (template->active_low)
		flags |= GPIOF_ACTIVE_LOW;

	ret = devm_gpio_request_one(dev, template->gpio, flags,
				    template->name);
	if (ret < 0)
		return ERR_PTR(ret);

	gpiod = gpio_to_desc(template->gpio);
	if (!gpiod)
		return ERR_PTR(-EINVAL);

	return gpiod;
}

static int gpio_led_probe(struct platform_device *pdev)
{
	struct gpio_led_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct gpio_leds_priv *priv;
	int i, ret = 0;

	if (pdata && pdata->num_leds) {
		priv = devm_kzalloc(&pdev->dev, struct_size(priv, leds, pdata->num_leds),
				    GFP_KERNEL);
		if (!priv)
			return -ENOMEM;

		priv->num_leds = pdata->num_leds;
		for (i = 0; i < priv->num_leds; i++) {
			const struct gpio_led *template = &pdata->leds[i];
			struct gpio_led_data *led_dat = &priv->leds[i];

			if (template->gpiod)
				led_dat->gpiod = template->gpiod;
			else
				led_dat->gpiod =
					gpio_led_get_gpiod(&pdev->dev,
							   i, template);
			if (IS_ERR(led_dat->gpiod)) {
				dev_info(&pdev->dev, "Skipping unavailable LED gpio %d (%s)\n",
					 template->gpio, template->name);
				continue;
			}

			ret = create_gpio_led(template, led_dat,
					      &pdev->dev, NULL,
					      pdata->gpio_blink_set);
			if (ret < 0)
				return ret;
		}
	} else {
		priv = gpio_leds_create(pdev);
		if (IS_ERR(priv))
			return PTR_ERR(priv);
	}

	platform_set_drvdata(pdev, priv);

	return 0;
}

static void gpio_led_shutdown(struct platform_device *pdev)
{
	struct gpio_leds_priv *priv = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < priv->num_leds; i++) {
		struct gpio_led_data *led = &priv->leds[i];

		if (!(led->cdev.flags & LED_RETAIN_AT_SHUTDOWN))
			gpio_led_set(&led->cdev, LED_OFF);
	}
}

static int gpio_led_remove(struct platform_device *pdev)
{
	struct gpio_leds_priv *priv = platform_get_drvdata(pdev);

	/* Driver core drops the device link right after ->remove(). */
	if (priv->shared_rail)
		gpio_leds_unregister_shared(&pdev->dev, priv);
	return 0;
}

static struct platform_driver gpio_led_driver = {
	.probe		= gpio_led_probe,
	.remove		= gpio_led_remove,
	.shutdown	= gpio_led_shutdown,
	.driver		= {
		.name	= "leds-gpio",
		.of_match_table = of_gpio_leds_match,
	},
};

module_platform_driver(gpio_led_driver);

MODULE_AUTHOR("Raphael Assenat <raph@8d.com>, Trent Piepho <tpiepho@freescale.com>");
MODULE_DESCRIPTION("GPIO LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-gpio");
