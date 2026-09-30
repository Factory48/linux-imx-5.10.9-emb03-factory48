// SPDX-License-Identifier: GPL-2.0
/* Dedicated EMB03 SAI3 machine: no HDMI input, ASRC or global jack state. */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <sound/jack.h>
#include <sound/soc.h>

struct emb03_card {
	struct snd_soc_card card;
	struct snd_soc_dai_link link;
	struct snd_soc_dai_link_component cpu, codec, platform;
	struct snd_soc_jack jack;
	struct snd_soc_jack_gpio gpio;
	struct snd_soc_jack_pin pins[3];
	bool jack_added;
	struct snd_soc_component *codec_component;
};
static const struct snd_soc_dapm_widget widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
};
static const struct snd_soc_dapm_route routes[] = {
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },
	/* Original DT reversed this input edge; signal flows Mic Jack -> MIC2. */
	{ "MIC2", NULL, "Mic Jack" },
};
static int link_init(struct snd_soc_pcm_runtime *rtd)
{
 struct clk *mclk;
 unsigned long rate;
 mclk = clk_get(asoc_rtd_to_codec(rtd, 0)->dev, "mclk");
 if (IS_ERR(mclk)) return PTR_ERR(mclk);
 rate = clk_get_rate(mclk);
 clk_put(mclk);
 return snd_soc_dai_set_sysclk(asoc_rtd_to_codec(rtd, 0), 0, rate, SND_SOC_CLOCK_IN);
}
static int machine_params(struct snd_pcm_substream *s, struct snd_pcm_hw_params *params)
{
 struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(s);
 int ret = snd_soc_dai_set_sysclk(asoc_rtd_to_cpu(rtd, 0), 1, 0, SND_SOC_CLOCK_IN);
 return ret == -ENOTSUPP ? 0 : ret;
}
static const struct snd_soc_ops machine_ops = { .hw_params = machine_params };
static int card_late_probe(struct snd_soc_card *card)
{
	struct emb03_card *p = snd_soc_card_get_drvdata(card);
	int ret;
	p->pins[0] = (struct snd_soc_jack_pin) {
		.pin = "Headphone Jack", .mask = SND_JACK_HEADPHONE };
	p->pins[1] = (struct snd_soc_jack_pin) {
		.pin = "Board Speaker", .mask = SND_JACK_HEADPHONE, .invert = 1 };
	/* Single shared detect line cannot classify a 3-pole versus 4-pole jack.
	 * Keep Mic Jack available for the built-in MIC2 path; report HP only.
	 */
	ret = snd_soc_card_jack_new(card, "Headphone Jack", SND_JACK_HEADPHONE,
				    &p->jack, p->pins, 2);
	if (ret)
		return ret;
	{
		struct snd_soc_pcm_runtime *rtd = list_first_entry(&card->rtd_list,
				struct snd_soc_pcm_runtime, list);
		p->codec_component = asoc_rtd_to_codec(rtd, 0)->component;
		ret = snd_soc_component_set_jack(p->codec_component, &p->jack, NULL);
		if (ret)
			return ret;
	}
	/* Original JSON has no speaker_ctl: initialize both inverse jack pins
	 * explicitly, including the no-headphone boot case (initial status=0).
	 */
	snd_soc_jack_report(&p->jack, 0, SND_JACK_HEADPHONE);
	snd_soc_dapm_enable_pin(&card->dapm, "Mic Jack");
	snd_soc_dapm_sync(&card->dapm);
	ret = snd_soc_jack_add_gpios(&p->jack, 1, &p->gpio);
	if (!ret)
		p->jack_added = true;
	return ret;
}
static int card_remove(struct snd_soc_card *card)
{
	struct emb03_card *p = snd_soc_card_get_drvdata(card);
	if (p->codec_component) {
		snd_soc_component_set_jack(p->codec_component, NULL, NULL);
		p->codec_component = NULL;
	}
	if (p->jack_added) {
		snd_soc_jack_free_gpios(&p->jack, 1, &p->gpio);
		p->jack_added = false;
	}
	return 0;
}
static void put_node(void *node) { of_node_put(node); }
static int card_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct emb03_card *p;
	enum of_gpio_flags flags, mic_flags;
	int ret, mic;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->cpu.of_node = of_parse_phandle(dev->of_node, "audio-cpu", 0);
	if (!p->cpu.of_node)
		return -EINVAL;
	ret = devm_add_action_or_reset(dev, put_node, p->cpu.of_node);
	if (ret)
		return ret;
	p->codec.of_node = of_parse_phandle(dev->of_node, "audio-codec", 0);
	if (!p->codec.of_node)
		return -EINVAL;
	ret = devm_add_action_or_reset(dev, put_node, p->codec.of_node);
	if (ret)
		return ret;
	p->gpio.gpio = of_get_named_gpio_flags(dev->of_node, "hp-det-gpio", 0, &flags);
	if (p->gpio.gpio < 0)
		return p->gpio.gpio;
	mic = of_get_named_gpio_flags(dev->of_node, "mic-det-gpio", 0, &mic_flags);
	if (mic < 0)
		return mic;
	/* Fail explicitly on another wiring rather than claiming headset support. */
	if (mic != p->gpio.gpio || mic_flags != flags)
		return -EINVAL;
	p->gpio.name = "emb03-headphone";
	p->gpio.report = SND_JACK_HEADPHONE;
	p->gpio.invert = !!(flags & OF_GPIO_ACTIVE_LOW);
	p->gpio.debounce_time = 200;
	p->platform.of_node = p->cpu.of_node;
	p->codec.dai_name = "es8316-hifi";
	p->link.name = "HiFi";
	p->link.stream_name = "HiFi";
	p->link.cpus = &p->cpu; p->link.num_cpus = 1;
	p->link.codecs = &p->codec; p->link.num_codecs = 1;
	p->link.platforms = &p->platform; p->link.num_platforms = 1;
	p->link.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM;
	p->link.init = link_init;
	p->link.ops = &machine_ops;
	p->card.dev = dev;
	p->card.owner = THIS_MODULE;
	p->card.name = "es8316-audio";
	p->card.dai_link = &p->link; p->card.num_links = 1;
	p->card.dapm_widgets = widgets; p->card.num_dapm_widgets = ARRAY_SIZE(widgets);
	p->card.dapm_routes = routes; p->card.num_dapm_routes = ARRAY_SIZE(routes);
	p->card.late_probe = card_late_probe;
	p->card.remove = card_remove;
	snd_soc_card_set_drvdata(&p->card, p);
	return devm_snd_soc_register_card(dev, &p->card);
}
static const struct of_device_id card_of[] = {
	{ .compatible = "fsl,imx-audio-es8316" }, { }
};
MODULE_DEVICE_TABLE(of, card_of);
static struct platform_driver emb03_machine_driver = {
	.probe = card_probe,
	.driver = { .name = "imx-emb03-audio", .of_match_table = card_of,
		.pm = &snd_soc_pm_ops },
};
module_platform_driver(emb03_machine_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EMB03 SAI3 ES8316 machine candidate");
