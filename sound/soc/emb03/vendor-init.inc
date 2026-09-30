/* GPL-2.0: cold probe only; NEVER called on resume. */
static int es8316_init_regs(struct snd_soc_component *component)
{
	int ret;
	ret = snd_soc_component_write(component, ES8316_RESET_REG00, 0x3f);
	if (ret < 0)
		return ret;
	usleep_range(5000, 5500);
	ret = snd_soc_component_write(component, ES8316_RESET_REG00, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_SYS_VMIDSEL_REG0C, 0xFF);
	if (ret < 0)
		return ret;
	msleep(30);
	ret = snd_soc_component_write(component, ES8316_CLKMGR_CLKSEL_REG02, 0x08);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CLKMGR_ADCOSR_REG03, 0x20);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CLKMGR_ADCDIV1_REG04, 0x11);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CLKMGR_ADCDIV2_REG05, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CLKMGR_DACDIV1_REG06, 0x11);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CLKMGR_DACDIV2_REG07, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CLKMGR_CPDIV_REG08, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_SDP_MS_BCKDIV_REG09, 0x04);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CLKMGR_CLKSW_REG01, 0x7F);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CAL_TYPE_REG1C, 0x0F);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CAL_HPLIV_REG1E, 0x90);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CAL_HPRIV_REG1F, 0x90);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_ADC_VOLUME_REG27, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_ADC_PDN_LINSEL_REG22, 0xc0);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_ADC_D2SEPGA_REG24, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_ADC_DMIC_REG25, 0x08);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_DAC_SET2_REG31, 0x20);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_DAC_SET3_REG32, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_DAC_VOLL_REG33, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_DAC_VOLR_REG34, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_SDP_ADCFMT_REG0A, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_SDP_DACFMT_REG0B, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_SYS_VMIDLOW_REG10, 0x11);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_SYS_VSEL_REG11, 0xFC);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_SYS_REF_REG12, 0x28);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_SYS_LP1_REG0E, 0x04);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_SYS_LP2_REG0F, 0x0C);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_DAC_PDN_REG2F, 0x11);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_HPMIX_SEL_REG13, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_HPMIX_SWITCH_REG14, 0x88);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_HPMIX_PDN_REG15, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_HPMIX_VOL_REG16, 0xBB);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CPHP_PDN2_REG1A, 0x10);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CPHP_LDOCTL_REG1B, 0x30);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CPHP_PDN1_REG19, 0x02);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_CPHP_ICAL_VOL_REG18, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_GPIO_SEL_REG4D, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_GPIO_DEBUNCE_INT_REG4E, 0x02);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_TESTMODE_REG50, 0xA0);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_TEST1_REG51, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_TEST2_REG52, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_SYS_PDN_REG0D, 0x00);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_RESET_REG00, 0xC0);
	if (ret < 0)
		return ret;
	msleep(50);
	ret = snd_soc_component_write(component, ES8316_ADC_PGAGAIN_REG23, 0x60);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_ADC_D2SEPGA_REG24, 0x01);
	if (ret < 0)
		return ret;
	/* adc ds mode, HPF enable */
	ret = snd_soc_component_write(component, ES8316_ADC_DMIC_REG25, 0x08);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_ADC_ALC1_REG29, 0xcd);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_ADC_ALC2_REG2A, 0x08);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_ADC_ALC3_REG2B, 0xa0);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_ADC_ALC4_REG2C, 0x05);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_ADC_ALC5_REG2D, 0x06);
	if (ret < 0)
		return ret;
	ret = snd_soc_component_write(component, ES8316_ADC_ALC6_REG2E, 0x61);
	if (ret < 0)
		return ret;
	return 0;
}

