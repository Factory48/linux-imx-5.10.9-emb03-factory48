// SPDX-License-Identifier: GPL-2.0
/* EMB03 candidate. Vendor register definitions/controls copyright Everest.
 * Factory-compatible PCM rates; codec master, unchanged board clock. See README.md.
 * Shared PMIC LDO4 is deliberately NOT owned or switched by this driver.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/jack.h>
#include "es8316.h"

struct emb03_codec {
	struct regmap *map;
	struct clk *mclk;
	struct gpio_desc *amp;
	struct mutex lock;
	struct delayed_work amp_work;
	bool muted, suspended, route_on;
	bool hp_inserted;
	struct snd_soc_jack *jack;
	struct notifier_block jack_nb;
	int pwr_count, io_error;
	unsigned int sysclk;
};

#include "vendor-controls.inc"
#include "vendor-init.inc"

static void amp_work(struct work_struct *work)
{
	struct emb03_codec *p = container_of(to_delayed_work(work),
					   struct emb03_codec, amp_work);
	mutex_lock(&p->lock);
	if (!p->muted && !p->suspended && !p->hp_inserted && p->route_on)
		gpiod_set_value_cansleep(p->amp, 1);
	mutex_unlock(&p->lock);
}

static int factory_jack_event(struct notifier_block *nb, unsigned long status, void *data)
{
	struct emb03_codec *p = container_of(nb, struct emb03_codec, jack_nb);

	mutex_lock(&p->lock);
	p->hp_inserted = !!(status & SND_JACK_HEADPHONE);
	if (p->hp_inserted)
		gpiod_set_value_cansleep(p->amp, 0);
	mutex_unlock(&p->lock);
	return NOTIFY_OK;
}

static int factory_set_jack(struct snd_soc_component *c, struct snd_soc_jack *jack, void *data)
{
	struct emb03_codec *p = snd_soc_component_get_drvdata(c);

	if (p->jack)
		snd_soc_jack_notifier_unregister(p->jack, &p->jack_nb);
	p->jack = jack;
	if (jack) {
		p->jack_nb.notifier_call = factory_jack_event;
		snd_soc_jack_notifier_register(jack, &p->jack_nb);
		factory_jack_event(&p->jack_nb, jack->status, jack);
	}
	return 0;
}

static int amp_event(struct snd_soc_dapm_widget *w,
		     struct snd_kcontrol *k, int event)
{
	struct snd_soc_component *c = snd_soc_dapm_to_component(w->dapm);
	struct emb03_codec *p = snd_soc_component_get_drvdata(c);

	mutex_lock(&p->lock);
	p->route_on = SND_SOC_DAPM_EVENT_ON(event);
	if (!p->route_on)
		gpiod_set_value_cansleep(p->amp, 0);
	else if (!p->muted && !p->suspended)
		mod_delayed_work(system_wq, &p->amp_work, msecs_to_jiffies(130));
	mutex_unlock(&p->lock);
	return 0;
}

static const struct snd_soc_dapm_widget amp_widgets[] = {
	SND_SOC_DAPM_SPK("Board Speaker", amp_event),
};
static const struct snd_soc_dapm_route amp_routes[] = {
	{ "Board Speaker", NULL, "HPOL" },
	{ "Board Speaker", NULL, "HPOR" },
};

static int codec_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct emb03_codec *p = snd_soc_component_get_drvdata(dai->component);
	int ret;

	mutex_lock(&p->lock);
	p->muted = true;
	gpiod_set_value_cansleep(p->amp, 0);
	if (mute)
		msleep(100);
	ret = regmap_update_bits(p->map, ES8316_DAC_SET1_REG30, 0x20,
				mute ? 0x20 : 0);
	if (!ret && !mute) {
		p->muted = false;
		if (!p->suspended && p->route_on)
			mod_delayed_work(system_wq, &p->amp_work,
					 msecs_to_jiffies(130));
	}
	mutex_unlock(&p->lock);
	return ret;
}

/* ASoC serializes stream callbacks. Keep first I/O error per callback. */
static int es8316_result(struct snd_soc_component *c)
{
 struct emb03_codec *p = snd_soc_component_get_drvdata(c);
 int ret = p->io_error;
 p->io_error = 0;
 return ret;
}
static void factory_write(struct snd_soc_component *c, unsigned int reg, unsigned int val)
{
 struct emb03_codec *p = snd_soc_component_get_drvdata(c);
 int ret = snd_soc_component_write(c, reg, val);
 if (ret < 0 && !p->io_error) p->io_error = ret;
}
static void factory_update(struct snd_soc_component *c, unsigned int reg, unsigned int mask, unsigned int val)
{
 struct emb03_codec *p = snd_soc_component_get_drvdata(c);
 int ret = snd_soc_component_update_bits(c, reg, mask, val);
 if (ret < 0 && !p->io_error) p->io_error = ret;
}
/* Factory set_sysclk: stores recognized frequencies, returns success for others.
 * No clk_set_rate, no PCM rate constraint, no coefficient-table programming.
 * Keep actual clock for diagnostics, never infer a new MCLK from params_rate.
 */
static int codec_sysclk(struct snd_soc_dai *dai, int id, unsigned int freq, int dir)
{
 struct emb03_codec *p = snd_soc_component_get_drvdata(dai->component);
 switch (freq) {
 case 11289600: case 18432000: case 22579200: case 36864000:
 case 12288000: case 19200000: case 16934400: case 24576000:
 case 33868800: case 12000000: case 24000000:
  p->sysclk = freq;
  break;
 default:
  /* Original unrecognized-frequency branch succeeds without updating state. */
  break;
 }
 dev_info(dai->dev, "factory sysclk metadata=%u actual=%lu id=%d dir=%d\n",
          freq, clk_get_rate(p->mclk), id, dir);
 return 0;
}
static int es8316_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u8 iface = 0;
	u8 adciface = 0;
	u8 daciface = 0;

	iface    = snd_soc_component_read(component, ES8316_IFACE);
	adciface = snd_soc_component_read(component, ES8316_ADC_IFACE);
	daciface = snd_soc_component_read(component, ES8316_DAC_IFACE);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		/* Factory codec-master divider selection. */
		iface = 0x88; //John_gao 0x84 -> 0x88
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		iface &= 0x7F;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		adciface &= 0xFC;
		daciface &= 0xFC;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		return -EINVAL;
	case SND_SOC_DAIFMT_LEFT_J:
		adciface &= 0xFC;
		daciface &= 0xFC;
		adciface |= 0x01;
		daciface |= 0x01;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		adciface &= 0xDC;
		daciface &= 0xDC;
		adciface |= 0x03;
		daciface |= 0x03;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		adciface &= 0xDC;
		daciface &= 0xDC;
		adciface |= 0x23;
		daciface |= 0x23;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		iface    &= 0xDF;
		adciface &= 0xDF;
		daciface &= 0xDF;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface    |= 0x20;
		adciface |= 0x20;
		daciface |= 0x20;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface    |= 0x20;
		adciface &= 0xDF;
		daciface &= 0xDF;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface    &= 0xDF;
		adciface |= 0x20;
		daciface |= 0x20;
		break;
	default:
		return -EINVAL;
	}
	factory_write(component, ES8316_IFACE, iface);
	factory_write(component, ES8316_ADC_IFACE, adciface);
	factory_write(component, ES8316_DAC_IFACE, daciface);
	return es8316_result(component);
}

/*
 * Headphone path power sequencing.
 *
 * Writing the whole chain at once makes an audible pop on both power-up and
 * power-down. Bring the charge pump up and let it settle before the mixer and
 * the output drivers are enabled, and tear down in the reverse order. The DAC
 * stays muted for the whole sequence (see codec_mute()), so only the analog
 * rails move. Register values are the same as the one-shot sequence; only the
 * order and the settle delays differ.
 */
static void es8316_hp_power_up(struct snd_soc_component *component)
{
	factory_write(component, ES8316_SYS_LP1_REG0E, 0x3F);
	factory_write(component, ES8316_SYS_LP2_REG0F, 0x1F);
	factory_write(component, ES8316_CPHP_PDN2_REG1A, 0x10);
	factory_write(component, ES8316_CPHP_LDOCTL_REG1B, 0x30);
	msleep(100);
	factory_write(component, ES8316_CPHP_PDN1_REG19, 0x02);
	msleep(20);
	factory_write(component, ES8316_HPMIX_SWITCH_REG14, 0x88);
	factory_write(component, ES8316_HPMIX_PDN_REG15, 0x00);
	factory_write(component, ES8316_HPMIX_VOL_REG16, 0xBB);
	factory_write(component, ES8316_DAC_PDN_REG2F, 0x00);
	msleep(30);
	/* charge pumps first, output drivers last */
	factory_write(component, ES8316_CPHP_OUTEN_REG17, 0x44);
	msleep(20);
	factory_write(component, ES8316_CPHP_OUTEN_REG17, 0x66);
}

static void es8316_hp_power_down(struct snd_soc_component *component)
{
	/* output drivers first, charge pumps last */
	factory_write(component, ES8316_CPHP_OUTEN_REG17, 0x44);
	msleep(20);
	factory_write(component, ES8316_CPHP_OUTEN_REG17, 0x00);
	msleep(30);
	factory_write(component, ES8316_DAC_PDN_REG2F, 0x11);
	factory_write(component, ES8316_HPMIX_SWITCH_REG14, 0x00);
	factory_write(component, ES8316_HPMIX_PDN_REG15, 0x33);
	factory_write(component, ES8316_HPMIX_VOL_REG16, 0x00);
	msleep(20);
	factory_write(component, ES8316_CPHP_PDN1_REG19, 0x06);
	msleep(20);
	factory_write(component, ES8316_CPHP_LDOCTL_REG1B, 0x03);
	factory_write(component, ES8316_CPHP_PDN2_REG1A, 0x22);
	msleep(20);
	factory_write(component, ES8316_SYS_LP1_REG0E, 0xFF);
	factory_write(component, ES8316_SYS_LP2_REG0F, 0xFF);
}

static int es8316_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct emb03_codec *es8316 = snd_soc_component_get_drvdata(component);
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	factory_write(component, ES8316_RESET_REG00, 0xC0);
	factory_write(component, ES8316_SYS_PDN_REG0D, 0x00);
	/* es8316: both playback and capture need dac mclk */
	factory_update(component, ES8316_CLKMGR_CLKSW_REG01,
			    ES8316_CLKMGR_MCLK_DIV_MASK |
			    ES8316_CLKMGR_DAC_MCLK_MASK,
			    ES8316_CLKMGR_MCLK_DIV_NML |
			    ES8316_CLKMGR_DAC_MCLK_EN);
	es8316->pwr_count++;

	if (playback) {
		es8316_hp_power_up(component);
		factory_update(component, ES8316_CLKMGR_CLKSW_REG01,
				    ES8316_CLKMGR_DAC_MCLK_MASK |
				    ES8316_CLKMGR_DAC_ANALOG_MASK,
				    ES8316_CLKMGR_DAC_MCLK_EN |
				    ES8316_CLKMGR_DAC_ANALOG_EN);
		msleep(50);
	} else {
		factory_update(component,
				    ES8316_ADC_PDN_LINSEL_REG22, 0xC0, 0x20);
		factory_update(component, ES8316_CLKMGR_CLKSW_REG01,
				    ES8316_CLKMGR_ADC_MCLK_MASK |
				    ES8316_CLKMGR_ADC_ANALOG_MASK,
				    ES8316_CLKMGR_ADC_MCLK_EN |
				    ES8316_CLKMGR_ADC_ANALOG_EN);
	}

	return es8316_result(component);
}

static void es8316_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	//struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component = dai->component;
	struct emb03_codec *es8316 = snd_soc_component_get_drvdata(component);
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	if (playback) {
		es8316_hp_power_down(component);
		factory_write(component, ES8316_SYS_PDN_REG0D, 0x00);
		snd_soc_component_update_bits (component, ES8316_CLKMGR_CLKSW_REG01,
				    ES8316_CLKMGR_DAC_ANALOG_MASK,
				    ES8316_CLKMGR_DAC_ANALOG_DIS);
	} else {
		factory_write(component, ES8316_ADC_PDN_LINSEL_REG22, 0xc0);
		snd_soc_component_update_bits (component, ES8316_CLKMGR_CLKSW_REG01,
				    ES8316_CLKMGR_ADC_MCLK_MASK |
				    ES8316_CLKMGR_ADC_ANALOG_MASK,
				    ES8316_CLKMGR_ADC_MCLK_DIS |
				    ES8316_CLKMGR_ADC_ANALOG_DIS);
	}

	if (es8316->pwr_count > 0 && --es8316->pwr_count == 0) {
		if (!es8316->hp_inserted)
			factory_write(component, ES8316_SYS_PDN_REG0D, 0x3F);
		factory_write(component, ES8316_CLKMGR_CLKSW_REG01, 0xF3);
	}
}


static int es8316_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int val = 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = ES8316_DACWL_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val = ES8316_DACWL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val = ES8316_DACWL_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val = ES8316_DACWL_32;
		break;
	default:
		val = ES8316_DACWL_16;
		break;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		factory_update(component, ES8316_SDP_DACFMT_REG0B,
				    ES8316_DACWL_MASK, val);
	else
		factory_update(component, ES8316_SDP_ADCFMT_REG0A,
				    ES8316_ADCWL_MASK, val);


//	printk("es8316 %s \n",__func__);
	//John_gao  set Lin2
	factory_write(component, ES8316_ADC_PDN_LINSEL_REG22, 0x10);
	return es8316_result(component);
}

static const struct snd_soc_dai_ops codec_ops = {
 .startup = es8316_pcm_startup, .shutdown = es8316_pcm_shutdown,
 .hw_params = es8316_pcm_hw_params, .set_fmt = es8316_set_dai_fmt,
 .set_sysclk = codec_sysclk, .mute_stream = codec_mute, .no_capture_mute = 1,
};
#define FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE)
static struct snd_soc_dai_driver codec_dai = {
	.name = "es8316-hifi",
	.playback = { .stream_name = "Playback", .channels_min = 1,
		.channels_max = 2, .rates = SNDRV_PCM_RATE_8000_96000, .formats = FORMATS },
	.capture = { .stream_name = "Capture", .channels_min = 1,
		.channels_max = 2, .rates = SNDRV_PCM_RATE_8000_96000, .formats = FORMATS },
	.ops = &codec_ops, .symmetric_rates = 1,
};

static int codec_suspend(struct snd_soc_component *c)
{
	struct emb03_codec *p = snd_soc_component_get_drvdata(c);
	mutex_lock(&p->lock);
	p->suspended = true;
	p->muted = true;
	gpiod_set_value_cansleep(p->amp, 0);
	mutex_unlock(&p->lock);
	cancel_delayed_work_sync(&p->amp_work);
	return regmap_update_bits(p->map, ES8316_DAC_SET1_REG30, 0x20, 0x20);
}

static int codec_resume(struct snd_soc_component *c)
{
	struct emb03_codec *p = snd_soc_component_get_drvdata(c);
	mutex_lock(&p->lock);
	p->suspended = false;
	/* Remain muted until ASoC explicitly unmutes a stream; no reset/reinit. */
	mutex_unlock(&p->lock);
	return 0;
}

static int codec_probe(struct snd_soc_component *c)
{
	struct emb03_codec *p = snd_soc_component_get_drvdata(c);
	int ret;

	ret = clk_prepare_enable(p->mclk);
	if (ret)
		return ret;
	ret = es8316_init_regs(c);
	if (ret)
		goto fail;
	ret = regmap_update_bits(p->map, ES8316_DAC_SET1_REG30, 0x20, 0x20);
	if (ret)
		goto fail;
	/* Board microphone is MIC2; do not force this again in hw_params. */
	ret = regmap_update_bits(p->map, ES8316_ADC_PDN_LINSEL_REG22, 0x30, 0x10);
	if (ret)
		goto fail;
	/* Safe initial volume, not the vendor's full-scale cold boot. */
	ret = regmap_write(p->map, ES8316_DAC_VOLL_REG33, 0x60);
	if (ret)
		goto fail;
	ret = regmap_write(p->map, ES8316_DAC_VOLR_REG34, 0x60);
	if (ret)
		goto fail;
	ret = snd_soc_dapm_new_controls(&c->dapm, amp_widgets, ARRAY_SIZE(amp_widgets));
	if (ret)
		goto fail;
	ret = snd_soc_dapm_add_routes(&c->dapm, amp_routes, ARRAY_SIZE(amp_routes));
	if (!ret)
		return 0;
fail:
	clk_disable_unprepare(p->mclk);
	return ret;
}
static void codec_remove(struct snd_soc_component *c)
{
	struct emb03_codec *p = snd_soc_component_get_drvdata(c);
	factory_set_jack(c, NULL, NULL);
	codec_suspend(c);
	clk_disable_unprepare(p->mclk);
}
static const struct snd_soc_component_driver codec_driver = {
	.probe = codec_probe, .remove = codec_remove,
	.set_jack = factory_set_jack,
	.suspend = codec_suspend, .resume = codec_resume,
	.controls = es8316_snd_controls, .num_controls = ARRAY_SIZE(es8316_snd_controls),
	.dapm_widgets = es8316_dapm_widgets, .num_dapm_widgets = ARRAY_SIZE(es8316_dapm_widgets),
	.dapm_routes = es8316_dapm_routes, .num_dapm_routes = ARRAY_SIZE(es8316_dapm_routes),
	.endianness = 1, .non_legacy_dai_naming = 1,
};
static const struct regmap_config map_config = {
	.reg_bits = 8, .val_bits = 8, .max_register = 0x53,
	/* No stale reset defaults: retained rail and direct checked transactions. */
	.cache_type = REGCACHE_NONE,
};
static int codec_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct emb03_codec *p = devm_kzalloc(&client->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->muted = true;
	mutex_init(&p->lock);
	INIT_DELAYED_WORK(&p->amp_work, amp_work);
	p->mclk = devm_clk_get(&client->dev, "mclk");
	if (IS_ERR(p->mclk))
		return PTR_ERR(p->mclk);
	p->amp = devm_gpiod_get_from_of_node(&client->dev, client->dev.of_node,
			"spk-con-gpio", 0, GPIOD_OUT_LOW, "es8316-speaker-mute");
	if (IS_ERR(p->amp))
		return PTR_ERR(p->amp);
	p->map = devm_regmap_init_i2c(client, &map_config);
	if (IS_ERR(p->map))
		return PTR_ERR(p->map);
	i2c_set_clientdata(client, p);
	return devm_snd_soc_register_component(&client->dev, &codec_driver, &codec_dai, 1);
}
static void codec_shutdown(struct i2c_client *client)
{
	struct emb03_codec *p = i2c_get_clientdata(client);
	mutex_lock(&p->lock);
	p->suspended = true;
	gpiod_set_value_cansleep(p->amp, 0);
	mutex_unlock(&p->lock);
	cancel_delayed_work_sync(&p->amp_work);
}
static const struct of_device_id codec_of[] = {
	{ .compatible = "everest,es8316" }, { }
};
MODULE_DEVICE_TABLE(of, codec_of);
static const struct i2c_device_id codec_ids[] = { { "es8316-emb03", 0 }, { } };
MODULE_DEVICE_TABLE(i2c, codec_ids);
static struct i2c_driver emb03_codec_driver = {
	.driver = { .name = "es8316-emb03", .of_match_table = codec_of },
	.probe = codec_i2c_probe, .shutdown = codec_shutdown, .id_table = codec_ids,
};
module_i2c_driver(emb03_codec_driver);
MODULE_DESCRIPTION("EMB03 ES8316 retained-power candidate");
MODULE_LICENSE("GPL");
