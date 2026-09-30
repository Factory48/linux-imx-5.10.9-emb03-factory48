/* GPL-2.0: reviewed DEBIX controls/DAPM; see README.md. */
static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -9600, 50, 1);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -9600, 50, 1);
static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(hpmixer_gain_tlv,
	0, 4, TLV_DB_SCALE_ITEM(-1200, 150, 0),
	8, 11, TLV_DB_SCALE_ITEM(-450, 150, 0),
);

static const DECLARE_TLV_DB_SCALE(mic_bst_tlv, 0, 1200, 0);

static unsigned int linin_pga_tlv[] = {
	TLV_DB_RANGE_HEAD(9),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(300, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(600, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(900, 0, 0),
	4, 4, TLV_DB_SCALE_ITEM(1200, 0, 0),
	5, 5, TLV_DB_SCALE_ITEM(1500, 0, 0),
	6, 6, TLV_DB_SCALE_ITEM(1800, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(2100, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(2400, 0, 0),
};
static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(hpout_vol_tlv,
	0, 0, TLV_DB_SCALE_ITEM(-4800, 0, 0),
	1, 3, TLV_DB_SCALE_ITEM(-2400, 1200, 0),
);


static const char *const alc_func_txt[] = { "Off", "On" };

static const struct soc_enum alc_func =
	SOC_ENUM_SINGLE(ES8316_ADC_ALC1_REG29, 6, 2, alc_func_txt);

static const char *const ng_type_txt[] = {
	"Constant PGA Gain", "Mute ADC Output" };

static const struct soc_enum ng_type =
	SOC_ENUM_SINGLE(ES8316_ADC_ALC6_REG2E, 6, 2, ng_type_txt);

static const char *const adcpol_txt[] = { "Normal", "Invert" };

static const struct soc_enum adcpol =
	SOC_ENUM_SINGLE(ES8316_ADC_MUTE_REG26, 1, 2, adcpol_txt);

static const char *const dacpol_txt[] = {
	"Normal", "R Invert", "L Invert", "L + R Invert" };

static const struct soc_enum dacpol =
	SOC_ENUM_SINGLE(ES8316_DAC_SET1_REG30, 0, 4, dacpol_txt);

static const struct snd_kcontrol_new es8316_snd_controls[] = {
	/* HP OUT VOLUME */
	SOC_DOUBLE_TLV("HP Playback Volume", ES8316_CPHP_ICAL_VOL_REG18,
		       4, 0, 3, 1, hpout_vol_tlv), //  4, 0, 4, 1, hpout_vol_tlv)
	/* HPMIXER VOLUME Control */
	SOC_DOUBLE_TLV("HPMixer Gain", ES8316_HPMIX_VOL_REG16,
		       0, 4, 11, 0, hpmixer_gain_tlv), // 0, 4, 7, 0, hpmixer_gain_tlv)

	/* DAC Digital controls */
	SOC_DOUBLE_R_TLV("DAC Playback Volume", ES8316_DAC_VOLL_REG33,
			 ES8316_DAC_VOLR_REG34, 0, 0xC0, 1, dac_vol_tlv),

	SOC_SINGLE("Enable DAC Soft Ramp", ES8316_DAC_SET1_REG30, 4, 1, 1),
	SOC_SINGLE("DAC Soft Ramp Rate", ES8316_DAC_SET1_REG30, 2, 4, 0),

	SOC_ENUM("Playback Polarity", dacpol),
	SOC_SINGLE("DAC Notch Filter", ES8316_DAC_SET2_REG31, 6, 1, 0),
	SOC_SINGLE("DAC Double Fs Mode", ES8316_DAC_SET2_REG31, 7, 1, 0),
	SOC_SINGLE("DAC Volume Control-LeR", ES8316_DAC_SET2_REG31, 2, 1, 0),
	SOC_SINGLE("DAC Stereo Enhancement", ES8316_DAC_SET3_REG32, 0, 7, 0),

	/* +20dB D2SE PGA Control */
	SOC_SINGLE_TLV("MIC Boost", ES8316_ADC_D2SEPGA_REG24,
		       0, 1, 0, mic_bst_tlv),
	/* 0-+24dB Lineinput PGA Control */
	SOC_SINGLE_TLV("Input PGA", ES8316_ADC_PGAGAIN_REG23,
		       4, 8, 0, linin_pga_tlv),

	/* ADC Digital  Control */
	SOC_SINGLE_TLV("ADC Capture Volume", ES8316_ADC_VOLUME_REG27,
		       0, 0xC0, 1, adc_vol_tlv),
	SOC_SINGLE("ADC Soft Ramp", ES8316_ADC_MUTE_REG26, 4, 1, 0),
	SOC_ENUM("Capture Polarity", adcpol),
	SOC_SINGLE("ADC Double FS Mode", ES8316_ADC_DMIC_REG25, 4, 1, 0),
	/* ADC ALC  Control */
	SOC_SINGLE("ALC Capture Target Volume",
		   ES8316_ADC_ALC3_REG2B, 4, 10, 0),
	SOC_SINGLE("ALC Capture Max PGA", ES8316_ADC_ALC1_REG29, 0, 28, 0),
	SOC_SINGLE("ALC Capture Min PGA", ES8316_ADC_ALC2_REG2A, 0, 28, 0),
	SOC_ENUM("ALC Capture Function", alc_func),
	SOC_SINGLE("ALC Capture Hold Time", ES8316_ADC_ALC3_REG2B, 0, 10, 0),
	SOC_SINGLE("ALC Capture Decay Time", ES8316_ADC_ALC4_REG2C, 4, 10, 0),
	SOC_SINGLE("ALC Capture Attack Time", ES8316_ADC_ALC4_REG2C, 0, 10, 0),
	SOC_SINGLE("ALC Capture NG Threshold", ES8316_ADC_ALC6_REG2E, 0, 31, 0),
	SOC_ENUM("ALC Capture NG Type", ng_type),
	SOC_SINGLE("ALC Capture NG Switch", ES8316_ADC_ALC6_REG2E, 5, 1, 0),
};

/* Analog Input MUX */
static const char * const es8316_analog_in_txt[] = {
	"lin1-rin1",
	"lin2-rin2",
	"lin1-rin1 with 20db Boost",
	"lin2-rin2 with 20db Boost"
};

static const unsigned int es8316_analog_in_values[] = { 0, 1, 2, 3 };

static const struct soc_enum es8316_analog_input_enum =
	SOC_VALUE_ENUM_SINGLE(ES8316_ADC_PDN_LINSEL_REG22, 4, 3,
			      ARRAY_SIZE(es8316_analog_in_txt),
			      es8316_analog_in_txt,
			      es8316_analog_in_values);

static const struct snd_kcontrol_new es8316_analog_in_mux_controls =
	SOC_DAPM_ENUM("Route", es8316_analog_input_enum);

/* Dmic MUX */
static const char * const es8316_dmic_txt[] = {
	"dmic disable",
	"dmic data at high level",
	"dmic data at low level",
};

static const unsigned int es8316_dmic_values[] = { 0, 1, 2 };

static const struct soc_enum es8316_dmic_src_enum =
	SOC_VALUE_ENUM_SINGLE(ES8316_ADC_DMIC_REG25, 0, 3,
			      ARRAY_SIZE(es8316_dmic_txt),
			      es8316_dmic_txt,
			      es8316_dmic_values);

static const struct snd_kcontrol_new es8316_dmic_src_controls =
	SOC_DAPM_ENUM("Route", es8316_dmic_src_enum);

/* hp mixer mux */
static const char *const es8316_hpmux_texts[] = {
	"lin1-rin1",
	"lin2-rin2",
	"lin-rin with Boost",
	"lin-rin with Boost and PGA"
};

static const unsigned int es8316_hpmux_values[] = { 0, 1, 2, 3 };

static const struct soc_enum es8316_left_hpmux_enum =
	SOC_VALUE_ENUM_SINGLE(ES8316_HPMIX_SEL_REG13, 4, 7,
			      ARRAY_SIZE(es8316_hpmux_texts),
			      es8316_hpmux_texts,
			      es8316_hpmux_values);

static const struct snd_kcontrol_new es8316_left_hpmux_controls =
	SOC_DAPM_ENUM("Route", es8316_left_hpmux_enum);

static const struct soc_enum es8316_right_hpmux_enum =
	SOC_VALUE_ENUM_SINGLE(ES8316_HPMIX_SEL_REG13, 0, 7,
			      ARRAY_SIZE(es8316_hpmux_texts),
			      es8316_hpmux_texts,
			      es8316_hpmux_values);

static const struct snd_kcontrol_new es8316_right_hpmux_controls =
	SOC_DAPM_ENUM("Route", es8316_right_hpmux_enum);

/* headphone Output Mixer */
static const struct snd_kcontrol_new es8316_out_left_mix[] = {
	SOC_DAPM_SINGLE("LLIN Switch", ES8316_HPMIX_SWITCH_REG14,
			6, 1, 0),
	SOC_DAPM_SINGLE("Left DAC Switch", ES8316_HPMIX_SWITCH_REG14,
			7, 1, 0),
};

static const struct snd_kcontrol_new es8316_out_right_mix[] = {
	SOC_DAPM_SINGLE("RLIN Switch", ES8316_HPMIX_SWITCH_REG14,
			2, 1, 0),
	SOC_DAPM_SINGLE("Right DAC Switch", ES8316_HPMIX_SWITCH_REG14,
			3, 1, 0),
};

/* DAC data source mux */
static const char * const es8316_dacsrc_texts[] = {
	"LDATA TO LDAC, RDATA TO RDAC",
	"LDATA TO LDAC, LDATA TO RDAC",
	"RDATA TO LDAC, RDATA TO RDAC",
	"RDATA TO LDAC, LDATA TO RDAC",
};

static const unsigned int es8316_dacsrc_values[] = { 0, 1, 2, 3 };

static const struct soc_enum es8316_dacsrc_mux_enum =
	SOC_VALUE_ENUM_SINGLE(ES8316_DAC_SET1_REG30, 6, 3,
			      ARRAY_SIZE(es8316_dacsrc_texts),
			      es8316_dacsrc_texts,
			      es8316_dacsrc_values);
static const struct snd_kcontrol_new es8316_dacsrc_mux_controls =
	SOC_DAPM_ENUM("Route", es8316_dacsrc_mux_enum);

static const struct snd_soc_dapm_widget es8316_dapm_widgets[] = {
	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC"),
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),

	SND_SOC_DAPM_MICBIAS("micbias", SND_SOC_NOPM,
			     0, 0),
	/* Input MUX */                    //John_gao 0 -> 1
	SND_SOC_DAPM_MUX("Differential Mux", SND_SOC_NOPM, 0, 0,
			 &es8316_analog_in_mux_controls),

	SND_SOC_DAPM_PGA("Line input PGA", ES8316_ADC_PDN_LINSEL_REG22,
			 7, 1, NULL, 0),

	/* ADCs */
	SND_SOC_DAPM_ADC("Mono ADC", NULL, ES8316_ADC_PDN_LINSEL_REG22, 6, 1),

	/* Dmic MUX */
	SND_SOC_DAPM_MUX("Digital Mic Mux", SND_SOC_NOPM, 0, 0,
			 &es8316_dmic_src_controls),

	/* Digital Interface */
	SND_SOC_DAPM_AIF_OUT("I2S OUT", "Capture",  1,
			     ES8316_SDP_ADCFMT_REG0A, 6, 1),

	SND_SOC_DAPM_AIF_IN("I2S IN", "Playback", 0,
			    SND_SOC_NOPM, 0, 0),

	/*  DACs DATA SRC MUX */
	SND_SOC_DAPM_MUX("DAC SRC Mux", SND_SOC_NOPM, 0, 0,
			 &es8316_dacsrc_mux_controls),
	/*  DACs  */
	SND_SOC_DAPM_DAC("Right DAC", NULL, ES8316_DAC_PDN_REG2F, 0, 1),
	SND_SOC_DAPM_DAC("Left DAC", NULL, ES8316_DAC_PDN_REG2F, 4, 1),

	/* Headphone Output Side */
	/* hpmux for hp mixer */
	SND_SOC_DAPM_MUX("Left Hp mux", SND_SOC_NOPM, 0, 0,
			 &es8316_left_hpmux_controls),
	SND_SOC_DAPM_MUX("Right Hp mux", SND_SOC_NOPM, 0, 0,
			 &es8316_right_hpmux_controls),
	/* Output mixer  */
	SND_SOC_DAPM_MIXER("Left Hp mixer", ES8316_HPMIX_PDN_REG15,
			   4, 1, &es8316_out_left_mix[0],
			   ARRAY_SIZE(es8316_out_left_mix)),
	SND_SOC_DAPM_MIXER("Right Hp mixer", ES8316_HPMIX_PDN_REG15,
			   0, 1, &es8316_out_right_mix[0],
			   ARRAY_SIZE(es8316_out_right_mix)),
	/* Output charge pump */

	SND_SOC_DAPM_PGA("HPCP L", ES8316_CPHP_OUTEN_REG17,
			 6, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPCP R", ES8316_CPHP_OUTEN_REG17,
			 2, 0, NULL, 0),
	/* Output Driver */
	SND_SOC_DAPM_PGA("HPVOL L", ES8316_CPHP_OUTEN_REG17,
			 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPVOL R", ES8316_CPHP_OUTEN_REG17,
			 1, 0, NULL, 0),
	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
};

static const struct snd_soc_dapm_route es8316_dapm_routes[] = {
	/*
	 * record route map
	 */
	{"MIC1", NULL, "micbias"},
	{"MIC2", NULL, "micbias"},
	{"DMIC", NULL, "micbias"},

	{"Differential Mux", "lin1-rin1", "MIC1"},
	{"Differential Mux", "lin2-rin2", "MIC2"},
	{"Differential Mux", "lin1-rin1 with 20db Boost", "MIC1"},
	{"Differential Mux", "lin2-rin2 with 20db Boost", "MIC2"},
	{"Line input PGA", NULL, "Differential Mux"},

	{"Mono ADC", NULL, "Line input PGA"},

	{"Digital Mic Mux", "dmic disable", "Mono ADC"},
	{"Digital Mic Mux", "dmic data at high level", "DMIC"},
	{"Digital Mic Mux", "dmic data at low level", "DMIC"},

	{"I2S OUT", NULL, "Digital Mic Mux"},
	/*
	 * playback route map
	 */
	{"DAC SRC Mux", "LDATA TO LDAC, RDATA TO RDAC", "I2S IN"},
	{"DAC SRC Mux", "LDATA TO LDAC, LDATA TO RDAC", "I2S IN"},
	{"DAC SRC Mux", "RDATA TO LDAC, RDATA TO RDAC", "I2S IN"},
	{"DAC SRC Mux", "RDATA TO LDAC, LDATA TO RDAC", "I2S IN"},

	{"Left DAC", NULL, "DAC SRC Mux"},
	{"Right DAC", NULL, "DAC SRC Mux"},

	{"Left Hp mux", "lin1-rin1", "MIC1"},
	{"Left Hp mux", "lin2-rin2", "MIC2"},
	{"Left Hp mux", "lin-rin with Boost", "Differential Mux"},
	{"Left Hp mux", "lin-rin with Boost and PGA", "Line input PGA"},

	{"Right Hp mux", "lin1-rin1", "MIC1"},
	{"Right Hp mux", "lin2-rin2", "MIC2"},
	{"Right Hp mux", "lin-rin with Boost", "Differential Mux"},
	{"Right Hp mux", "lin-rin with Boost and PGA", "Line input PGA"},

	{"Left Hp mixer", "LLIN Switch", "Left Hp mux"},
	{"Left Hp mixer", "Left DAC Switch", "Left DAC"},

	{"Right Hp mixer", "RLIN Switch", "Right Hp mux"},
	{"Right Hp mixer", "Right DAC Switch", "Right DAC"},

	{"HPCP L", NULL, "Left Hp mixer"},
	{"HPCP R", NULL, "Right Hp mixer"},

	{"HPVOL L", NULL, "HPCP L"},
	{"HPVOL R", NULL, "HPCP R"},

	{"HPOL", NULL, "HPVOL L"},
	{"HPOR", NULL, "HPVOL R"},
};

