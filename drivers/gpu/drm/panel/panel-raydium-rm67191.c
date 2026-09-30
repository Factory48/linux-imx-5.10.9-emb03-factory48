// SPDX-License-Identifier: GPL-2.0
/*
 * Raydium RM67191 MIPI-DSI panel driver
 *
 * Copyright 2019 NXP
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/emb03-panel.h>
#include <linux/mutex.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

/* Panel specific color-format bits */
#define COL_FMT_16BPP 0x55
#define COL_FMT_18BPP 0x66
#define COL_FMT_24BPP 0x77

/* Write Manufacture Command Set Control */
#define WRMAUCCTR 0xFE

/* Manufacturer Command Set pages (CMD2) */
struct cmd_set_entry {
	u8 cmd;
	u8 param;
};

/*
 * There is no description in the Reference Manual about these commands.
 * We received them from vendor, so just use them as is.
 */
static const struct cmd_set_entry mcs_rm67191[] = {
	{0xFE, 0x0B}, {0x28, 0x40}, {0x29, 0x4F}, {0xFE, 0x0E},
	{0x4B, 0x00}, {0x4C, 0x0F}, {0x4D, 0x20}, {0x4E, 0x40},
	{0x4F, 0x60}, {0x50, 0xA0}, {0x51, 0xC0}, {0x52, 0xE0},
	{0x53, 0xFF}, {0xFE, 0x0D}, {0x18, 0x08}, {0x42, 0x00},
	{0x08, 0x41}, {0x46, 0x02}, {0x72, 0x09}, {0xFE, 0x0A},
	{0x24, 0x17}, {0x04, 0x07}, {0x1A, 0x0C}, {0x0F, 0x44},
	{0xFE, 0x04}, {0x00, 0x0C}, {0x05, 0x08}, {0x06, 0x08},
	{0x08, 0x08}, {0x09, 0x08}, {0x0A, 0xE6}, {0x0B, 0x8C},
	{0x1A, 0x12}, {0x1E, 0xE0}, {0x29, 0x93}, {0x2A, 0x93},
	{0x2F, 0x02}, {0x31, 0x02}, {0x33, 0x05}, {0x37, 0x2D},
	{0x38, 0x2D}, {0x3A, 0x1E}, {0x3B, 0x1E}, {0x3D, 0x27},
	{0x3F, 0x80}, {0x40, 0x40}, {0x41, 0xE0}, {0x4F, 0x2F},
	{0x50, 0x1E}, {0xFE, 0x06}, {0x00, 0xCC}, {0x05, 0x05},
	{0x07, 0xA2}, {0x08, 0xCC}, {0x0D, 0x03}, {0x0F, 0xA2},
	{0x32, 0xCC}, {0x37, 0x05}, {0x39, 0x83}, {0x3A, 0xCC},
	{0x41, 0x04}, {0x43, 0x83}, {0x44, 0xCC}, {0x49, 0x05},
	{0x4B, 0xA2}, {0x4C, 0xCC}, {0x51, 0x03}, {0x53, 0xA2},
	{0x75, 0xCC}, {0x7A, 0x03}, {0x7C, 0x83}, {0x7D, 0xCC},
	{0x82, 0x02}, {0x84, 0x83}, {0x85, 0xEC}, {0x86, 0x0F},
	{0x87, 0xFF}, {0x88, 0x00}, {0x8A, 0x02}, {0x8C, 0xA2},
	{0x8D, 0xEA}, {0x8E, 0x01}, {0x8F, 0xE8}, {0xFE, 0x06},
	{0x90, 0x0A}, {0x92, 0x06}, {0x93, 0xA0}, {0x94, 0xA8},
	{0x95, 0xEC}, {0x96, 0x0F}, {0x97, 0xFF}, {0x98, 0x00},
	{0x9A, 0x02}, {0x9C, 0xA2}, {0xAC, 0x04}, {0xFE, 0x06},
	{0xB1, 0x12}, {0xB2, 0x17}, {0xB3, 0x17}, {0xB4, 0x17},
	{0xB5, 0x17}, {0xB6, 0x11}, {0xB7, 0x08}, {0xB8, 0x09},
	{0xB9, 0x06}, {0xBA, 0x07}, {0xBB, 0x17}, {0xBC, 0x17},
	{0xBD, 0x17}, {0xBE, 0x17}, {0xBF, 0x17}, {0xC0, 0x17},
	{0xC1, 0x17}, {0xC2, 0x17}, {0xC3, 0x17}, {0xC4, 0x0F},
	{0xC5, 0x0E}, {0xC6, 0x00}, {0xC7, 0x01}, {0xC8, 0x10},
	{0xFE, 0x06}, {0x95, 0xEC}, {0x8D, 0xEE}, {0x44, 0xEC},
	{0x4C, 0xEC}, {0x32, 0xEC}, {0x3A, 0xEC}, {0x7D, 0xEC},
	{0x75, 0xEC}, {0x00, 0xEC}, {0x08, 0xEC}, {0x85, 0xEC},
	{0xA6, 0x21}, {0xA7, 0x05}, {0xA9, 0x06}, {0x82, 0x06},
	{0x41, 0x06}, {0x7A, 0x07}, {0x37, 0x07}, {0x05, 0x06},
	{0x49, 0x06}, {0x0D, 0x04}, {0x51, 0x04},
};

static const struct cmd_set_entry mcs_rm67199[] = {
	{0xFE, 0xA0}, {0x2B, 0x18}, {0xFE, 0x70}, {0x7D, 0x05},
	{0x5D, 0x0A}, {0x5A, 0x79}, {0x5C, 0x00}, {0x52, 0x00},
	{0xFE, 0xD0}, {0x40, 0x02}, {0x13, 0x40}, {0xFE, 0x40},
	{0x05, 0x08}, {0x06, 0x08}, {0x08, 0x08}, {0x09, 0x08},
	{0x0A, 0xCA}, {0x0B, 0x88}, {0x20, 0x93}, {0x21, 0x93},
	{0x24, 0x02}, {0x26, 0x02}, {0x28, 0x05}, {0x2A, 0x05},
	{0x74, 0x2F}, {0x75, 0x1E}, {0xAD, 0x00}, {0xFE, 0x60},
	{0x00, 0xCC}, {0x01, 0x00}, {0x02, 0x04}, {0x03, 0x00},
	{0x04, 0x00}, {0x05, 0x07}, {0x06, 0x00}, {0x07, 0x88},
	{0x08, 0x00}, {0x09, 0xCC}, {0x0A, 0x00}, {0x0B, 0x04},
	{0x0C, 0x00}, {0x0D, 0x00}, {0x0E, 0x05}, {0x0F, 0x00},
	{0x10, 0x88}, {0x11, 0x00}, {0x12, 0xCC}, {0x13, 0x0F},
	{0x14, 0xFF}, {0x15, 0x04}, {0x16, 0x00}, {0x17, 0x06},
	{0x18, 0x00}, {0x19, 0x96}, {0x1A, 0x00}, {0x24, 0xCC},
	{0x25, 0x00}, {0x26, 0x02}, {0x27, 0x00}, {0x28, 0x00},
	{0x29, 0x06}, {0x2A, 0x06}, {0x2B, 0x82}, {0x2D, 0x00},
	{0x2F, 0xCC}, {0x30, 0x00}, {0x31, 0x02}, {0x32, 0x00},
	{0x33, 0x00}, {0x34, 0x07}, {0x35, 0x06}, {0x36, 0x82},
	{0x37, 0x00}, {0x38, 0xCC}, {0x39, 0x00}, {0x3A, 0x02},
	{0x3B, 0x00}, {0x3D, 0x00}, {0x3F, 0x07}, {0x40, 0x00},
	{0x41, 0x88}, {0x42, 0x00}, {0x43, 0xCC}, {0x44, 0x00},
	{0x45, 0x02}, {0x46, 0x00}, {0x47, 0x00}, {0x48, 0x06},
	{0x49, 0x02}, {0x4A, 0x8A}, {0x4B, 0x00}, {0x5F, 0xCA},
	{0x60, 0x01}, {0x61, 0xE8}, {0x62, 0x09}, {0x63, 0x00},
	{0x64, 0x07}, {0x65, 0x00}, {0x66, 0x30}, {0x67, 0x00},
	{0x9B, 0x03}, {0xA9, 0x07}, {0xAA, 0x06}, {0xAB, 0x02},
	{0xAC, 0x10}, {0xAD, 0x11}, {0xAE, 0x05}, {0xAF, 0x04},
	{0xB0, 0x10}, {0xB1, 0x10}, {0xB2, 0x10}, {0xB3, 0x10},
	{0xB4, 0x10}, {0xB5, 0x10}, {0xB6, 0x10}, {0xB7, 0x10},
	{0xB8, 0x10}, {0xB9, 0x10}, {0xBA, 0x04}, {0xBB, 0x05},
	{0xBC, 0x00}, {0xBD, 0x01}, {0xBE, 0x0A}, {0xBF, 0x10},
	{0xC0, 0x11}, {0xFE, 0xA0}, {0x22, 0x00},
};

static const u32 rad_bus_formats[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB666_1X18,
	MEDIA_BUS_FMT_RGB565_1X16,
};

static const u32 rad_bus_flags = DRM_BUS_FLAG_DE_LOW |
				 DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE;

struct rad_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;

	struct gpio_desc *reset;
	struct gpio_desc *pwd;
	struct backlight_device *backlight;
	struct backlight_device *external_backlight;
	bool backlight_delay_done;

	struct regulator_bulk_data *supplies;
	unsigned int num_supplies;

	bool prepared;
	bool enabled;

	const struct rad_platform_data *pdata;
};

struct rad_platform_data {
	int (*enable)(struct rad_panel *panel);
	const struct drm_display_mode *mode;
	bool emb03;
};

/* Single EMB03 panel provider. Lock order: state_lock -> touch lifecycle.
 * No touch worker takes state_lock or enters DRM. Holding this mutex across
 * OFF callbacks guarantees bus/IRQ drain precedes panel power/reset changes.
 */
static DEFINE_MUTEX(emb03_state_lock);
static struct rad_panel *emb03_owner;
static struct emb03_panel_listener *emb03_listener;
static bool emb03_ready;

int emb03_panel_register_listener(struct emb03_panel_listener *listener)
{
	int ret = 0;

	if (!listener || !listener->notify)
		return -EINVAL;
	mutex_lock(&emb03_state_lock);
	if (emb03_listener) {
		ret = -EBUSY;
	} else {
		emb03_listener = listener;
		listener->notify(listener, emb03_ready);
	}
	mutex_unlock(&emb03_state_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(emb03_panel_register_listener);

void emb03_panel_unregister_listener(struct emb03_panel_listener *listener)
{
	mutex_lock(&emb03_state_lock);
	if (emb03_listener == listener)
		emb03_listener = NULL;
	mutex_unlock(&emb03_state_lock);
}
EXPORT_SYMBOL_GPL(emb03_panel_unregister_listener);

static void emb03_panel_state(struct rad_panel *rad, bool ready)
{
	if (!rad->pdata->emb03)
		return;
	mutex_lock(&emb03_state_lock);
	if (emb03_owner == rad && emb03_ready != ready) {
		emb03_ready = ready;
		if (emb03_listener)
			emb03_listener->notify(emb03_listener, ready);
	}
	mutex_unlock(&emb03_state_lock);
}

static void emb03_panel_release(void *data)
{
	struct rad_panel *rad = data;

	mutex_lock(&emb03_state_lock);
	if (emb03_owner == rad) {
		emb03_ready = false;
		if (emb03_listener)
			emb03_listener->notify(emb03_listener, false);
		emb03_owner = NULL;
	}
	mutex_unlock(&emb03_state_lock);
}

static int emb03_panel_claim(struct rad_panel *rad)
{
	int ret = 0;

	mutex_lock(&emb03_state_lock);
	if (emb03_owner)
		ret = -EBUSY;
	else
		emb03_owner = rad;
	mutex_unlock(&emb03_state_lock);
	return ret;
}

static const struct drm_display_mode default_mode = {
	.clock = 121000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 20,
	.hsync_end = 1080 + 20 + 2,
	.htotal = 1080 + 20 + 2 + 34,
	.vdisplay = 1920,
	.vsync_start = 1920 + 10,
	.vsync_end = 1920 + 10 + 2,
	.vtotal = 1920 + 10 + 2 + 4,
	.width_mm = 68,
	.height_mm = 121,
	.flags = DRM_MODE_FLAG_NHSYNC |
		 DRM_MODE_FLAG_NVSYNC,
};

/*
 * EMB03 1.2.2 board variant, reconstructed from the hash-pinned kernel.
 * rad_panel_get_modes -> default_mode at 0xffff800011a0c6d8.
 * See evidence/panel-port-final.md for provenance and lifecycle differences.
 * The state dump's 0x48 is mode TYPE, not sync flags (flags are zero).
 * Opt in with factory48,emb03-rm67191 (or legacy polyhex,emb03-rm67191); generic panels stay unchanged.
 */
static const struct drm_display_mode emb03_mode = {
	.clock = 80290,
	.hdisplay = 720,
	.hsync_start = 780,
	.hsync_end = 920,
	.htotal = 928,
	.vdisplay = 1280,
	.vsync_start = 1430,
	.vsync_end = 1432,
	.vtotal = 1442,
	.width_mm = 68,
	.height_mm = 121,
	.flags = 0,
};

static inline struct rad_panel *to_rad_panel(struct drm_panel *panel)
{
	return container_of(panel, struct rad_panel, panel);
}

static int rad_panel_push_cmd_list(struct mipi_dsi_device *dsi,
				   struct cmd_set_entry const *cmd_set,
				   size_t count)
{
	size_t i;
	int ret = 0;

	for (i = 0; i < count; i++) {
		const struct cmd_set_entry *entry = cmd_set++;
		u8 buffer[2] = { entry->cmd, entry->param };

		ret = mipi_dsi_generic_write(dsi, &buffer, sizeof(buffer));
		if (ret < 0)
			return ret;
	}

	return ret;
};

static int color_format_from_dsi_format(enum mipi_dsi_pixel_format format)
{
	switch (format) {
	case MIPI_DSI_FMT_RGB565:
		return COL_FMT_16BPP;
	case MIPI_DSI_FMT_RGB666:
	case MIPI_DSI_FMT_RGB666_PACKED:
		return COL_FMT_18BPP;
	case MIPI_DSI_FMT_RGB888:
		return COL_FMT_24BPP;
	default:
		return COL_FMT_24BPP; /* for backward compatibility */
	}
};

static int rad_panel_prepare(struct drm_panel *panel)
{
	struct rad_panel *rad = to_rad_panel(panel);
	int ret;

	if (rad->prepared)
		return 0;

	emb03_panel_state(rad, false);
	ret = regulator_bulk_enable(rad->num_supplies, rad->supplies);
	if (ret)
		return ret;

	/* At lest 10ms needed between power-on and reset-out as RM specifies */
	usleep_range(10000, 12000);

	if (rad->reset) {
		gpiod_set_value_cansleep(rad->reset, 0);
		/*
		 * 50ms delay after reset-out, as per manufacturer initalization
		 * sequence.
		 */
		msleep(50);
	}

	rad->prepared = true;

	return 0;
}

static int rad_panel_unprepare(struct drm_panel *panel)
{
	struct rad_panel *rad = to_rad_panel(panel);
	int ret;

	emb03_panel_state(rad, false);
	if (!rad->prepared)
		return 0;

	/*
	 * Right after asserting the reset, we need to release it, so that the
	 * touch driver can have an active connection with the touch controller
	 * even after the display is turned off.
	 */
	if (rad->reset) {
		gpiod_set_value_cansleep(rad->reset, 1);
		usleep_range(15000, 17000);
		gpiod_set_value_cansleep(rad->reset, 0);
	}

	ret = regulator_bulk_disable(rad->num_supplies, rad->supplies);
	if (ret)
		return ret;

	rad->prepared = false;
	if (rad->pdata->emb03)
		gpiod_set_value_cansleep(rad->pwd, 0);

	return 0;
}

/* Exact 1.2.2 DCS buffers, NOT the 0.1.6 table. Length includes opcode. */
struct emb03_dcs_cmd {
	u8 len;
	u8 data[31];
};

static const struct emb03_dcs_cmd emb03_dcs[] = {
	{ 10, { 0xbb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5a, 0xa5, 0x0f } },
	{ 31, { 0xc1, 0x53, 0xe0, 0x02, 0x02, 0x71, 0x05, 0x22, 0x80, 0x00, 0x30, 0x00, 0x00, 0x40, 0x03, 0xff, 0xff, 0x01, 0x03, 0x00, 0x55, 0x4f, 0x52, 0x04, 0x15, 0x08, 0x50, 0x10, 0x10, 0x00, 0x00 } },
	{ 10, { 0xc8, 0x03, 0x14, 0x14, 0x18, 0x72, 0x18, 0x72, 0x00, 0x00 } },
	{ 3, { 0xc4, 0x9d, 0x00 } },
	{ 11, { 0xc9, 0x1a, 0x03, 0x3e, 0x60, 0x12, 0xee, 0x44, 0x66, 0x88, 0x80 } },
	{ 17, { 0xcd, 0x8a, 0x20, 0x02, 0x02, 0x11, 0x30, 0x80, 0x08, 0xf5, 0x08, 0x00, 0x01, 0x01, 0x01, 0x01, 0xaa } },
	{ 21, { 0xce, 0x80, 0x00, 0xff, 0xb6, 0x00, 0x04, 0x00, 0x04, 0x08, 0x08, 0x00, 0x04, 0x38, 0xd0, 0x42, 0x80, 0x00, 0x75, 0x00, 0x00 } },
	{ 14, { 0xcf, 0x00, 0x00, 0x35, 0x1f, 0xad, 0x05, 0x05, 0x00, 0x30, 0x10, 0x48, 0xa0, 0x41 } },
	{ 13, { 0xd0, 0x10, 0x00, 0x44, 0x72, 0x74, 0xa6, 0x52, 0xb2, 0x40, 0x04, 0x71, 0x00 } },
	{ 30, { 0xd3, 0x00, 0x04, 0x08, 0x0c, 0x0d, 0x10, 0x13, 0x11, 0x47, 0x47, 0x4d, 0x52, 0x58, 0x61, 0x6d, 0x7f, 0x20, 0x32, 0x41, 0x40, 0x48, 0x58, 0x5b, 0x5e, 0x6a, 0x6c, 0x68, 0x7a, 0x8c } },
	{ 30, { 0xd4, 0x00, 0x04, 0x08, 0x0c, 0x0d, 0x10, 0x13, 0x11, 0x47, 0x47, 0x4d, 0x52, 0x58, 0x61, 0x6d, 0x7f, 0x20, 0x32, 0x41, 0x40, 0x48, 0x58, 0x5b, 0x5e, 0x6a, 0x6c, 0x68, 0x7a, 0x8c } },
	{ 31, { 0xd5, 0x60, 0x40, 0x00, 0x00, 0x44, 0x44, 0x08, 0x07, 0x00, 0x08, 0x07, 0x00, 0x30, 0x00, 0xd5, 0x1f, 0x03, 0x03, 0x03, 0x03, 0x00, 0x03, 0x03, 0x00, 0x84, 0xa7, 0x00, 0x60, 0x40, 0x01 } },
	{ 27, { 0xd6, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x07, 0x07, 0x00, 0x09, 0x51, 0x05, 0x03, 0x03, 0x03, 0x03, 0x03, 0x30, 0x02, 0x01 } },
	{ 23, { 0xd7, 0x1f, 0x1f, 0x1f, 0x08, 0x0a, 0x0c, 0x0e, 0x1f, 0x1e, 0x1e, 0x00, 0x02, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f } },
	{ 23, { 0xd8, 0x1f, 0x1f, 0x1f, 0x09, 0x0b, 0x0d, 0x0f, 0x1f, 0x1e, 0x1e, 0x01, 0x03, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f } },
	{ 23, { 0xd9, 0x1f, 0x1f, 0x1e, 0x0f, 0x0d, 0x0b, 0x09, 0x1f, 0x1e, 0x1f, 0x03, 0x01, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f } },
	{ 23, { 0xdd, 0x1f, 0x1f, 0x1e, 0x0e, 0x0c, 0x0a, 0x08, 0x1f, 0x1e, 0x1f, 0x02, 0x00, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f } },
};

static int emb03_enable(struct rad_panel *rad)
{
	struct mipi_dsi_device *dsi = rad->dsi;
	int ret;
	size_t i;

	if (rad->enabled)
		return 0;
	if (!rad->prepared)
		return -EPERM;

	gpiod_set_value_cansleep(rad->pwd, 1);
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	ret = mipi_dsi_dcs_set_pixel_format(dsi,
			color_format_from_dsi_format(dsi->format));
	if (ret < 0)
		goto fail;
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0)
		goto fail;
	msleep(10);
	for (i = 0; i < ARRAY_SIZE(emb03_dcs); i++) {
		ret = mipi_dsi_dcs_write_buffer(dsi, emb03_dcs[i].data,
					      emb03_dcs[i].len);
		if (ret < 0)
			goto fail;
		/* SEC DSI returns packet size (payload + 4), not tx_len. */
	}
	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0)
		goto fail;
	ret = backlight_enable(rad->backlight);
	if (ret < 0)
		goto fail;
	ret = backlight_enable(rad->external_backlight);
	if (ret < 0)
		goto fail;
	rad->enabled = true;
	emb03_panel_state(rad, true);
	return 0;

fail:
	emb03_panel_state(rad, false);
	dev_err(&dsi->dev, "EMB03 enable failed: %d\n", ret);
	backlight_disable(rad->backlight);
	backlight_disable(rad->external_backlight);
	gpiod_set_value_cansleep(rad->reset, 1);
	/* Leave supplies/pwd for the normal unprepare path, as in 1.2.2. */
	return ret;
}

static int rm67191_enable(struct rad_panel *panel)
{
	struct mipi_dsi_device *dsi = panel->dsi;
	struct device *dev = &dsi->dev;
	int color_format = color_format_from_dsi_format(dsi->format);
	int ret;

	if (panel->enabled)
		return 0;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = rad_panel_push_cmd_list(dsi,
				      &mcs_rm67191[0],
				      ARRAY_SIZE(mcs_rm67191));
	if (ret < 0) {
		dev_err(dev, "Failed to send MCS (%d)\n", ret);
		goto fail;
	}

	/* Select User Command Set table (CMD1) */
	ret = mipi_dsi_generic_write(dsi, (u8[]){ WRMAUCCTR, 0x00 }, 2);
	if (ret < 0)
		goto fail;

	/* Software reset */
	ret = mipi_dsi_dcs_soft_reset(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to do Software Reset (%d)\n", ret);
		goto fail;
	}

	usleep_range(15000, 17000);

	/* Set DSI mode */
	ret = mipi_dsi_generic_write(dsi, (u8[]){ 0xC2, 0x0B }, 2);
	if (ret < 0) {
		dev_err(dev, "Failed to set DSI mode (%d)\n", ret);
		goto fail;
	}
	/* Set tear ON */
	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear ON (%d)\n", ret);
		goto fail;
	}
	/* Set tear scanline */
	ret = mipi_dsi_dcs_set_tear_scanline(dsi, 0x380);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear scanline (%d)\n", ret);
		goto fail;
	}
	/* Set pixel format */
	ret = mipi_dsi_dcs_set_pixel_format(dsi, color_format);
	dev_dbg(dev, "Interface color format set to 0x%x\n", color_format);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format (%d)\n", ret);
		goto fail;
	}
	/* Exit sleep mode */
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode (%d)\n", ret);
		goto fail;
	}

	usleep_range(5000, 7000);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display ON (%d)\n", ret);
		goto fail;
	}

	backlight_enable(panel->backlight);

	panel->enabled = true;

	return 0;

fail:
	gpiod_set_value_cansleep(panel->reset, 1);

	return ret;
}

static int rm67199_enable(struct rad_panel *panel)
{
	struct mipi_dsi_device *dsi = panel->dsi;
	struct device *dev = &dsi->dev;
	int color_format = color_format_from_dsi_format(dsi->format);
	int ret;

	if (panel->enabled)
		return 0;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = rad_panel_push_cmd_list(dsi,
				      &mcs_rm67199[0],
				      ARRAY_SIZE(mcs_rm67199));
	if (ret < 0) {
		dev_err(dev, "Failed to send MCS (%d)\n", ret);
		goto fail;
	}

	/* Select User Command Set table (CMD1) */
	ret = mipi_dsi_generic_write(dsi, (u8[]){ WRMAUCCTR, 0x00 }, 2);
	if (ret < 0)
		goto fail;

	/* Set DSI mode */
	ret = mipi_dsi_generic_write(dsi, (u8[]){ 0xC2, 0x08 }, 2);
	if (ret < 0) {
		dev_err(dev, "Failed to set DSI mode (%d)\n", ret);
		goto fail;
	}
	/* Set tear ON */
	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear ON (%d)\n", ret);
		goto fail;
	}
	/* Set tear scanline */
	ret = mipi_dsi_dcs_set_tear_scanline(dsi, 0x00);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear scanline (%d)\n", ret);
		goto fail;
	}
	/* Set pixel format */
	ret = mipi_dsi_dcs_set_pixel_format(dsi, color_format);
	dev_dbg(dev, "Interface color format set to 0x%x\n",
			     color_format);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format (%d)\n", ret);
		goto fail;
	}
	/* Exit sleep mode */
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode (%d)\n", ret);
		goto fail;
	}

	/*
	 * Although, 120ms seems a lot, this is the amount of delay that the
	 * manufacturer suggests it should be used between the sleep-out and
	 * display-on commands
	 */
	msleep(120);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display ON (%d)\n", ret);
		goto fail;
	}

	/*
	 * Also, 100ms delay between display-on and backlight enable as per
	 * manufacturer initialization sequence.
	 */
	msleep(100);

	backlight_enable(panel->backlight);

	panel->enabled = true;

	return 0;

fail:
	gpiod_set_value_cansleep(panel->reset, 1);

	return ret;
}

static int rad_panel_enable(struct drm_panel *panel)
{
	struct rad_panel *rad = to_rad_panel(panel);

	return rad->pdata->enable(rad);
}

static int rad_panel_disable(struct drm_panel *panel)
{
	struct rad_panel *rad = to_rad_panel(panel);
	struct mipi_dsi_device *dsi = rad->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	emb03_panel_state(rad, false);
	if (!rad->enabled)
		return 0;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	backlight_disable(rad->backlight);
	if (rad->pdata->emb03)
		backlight_disable(rad->external_backlight);

	/* Backlight callbacks use HS; send the shutdown DCS in LP mode. */
	if (rad->pdata->emb03)
		dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	usleep_range(10000, 12000);

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display OFF (%d)\n", ret);
		return ret;
	}

	usleep_range(5000, 10000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode (%d)\n", ret);
		return ret;
	}

	rad->enabled = false;

	return 0;
}

static int rad_panel_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct rad_panel *rad = to_rad_panel(panel);
	const struct drm_display_mode *native = rad->pdata->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, native);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			native->hdisplay, native->vdisplay,
			drm_mode_vrefresh(native));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	connector->display_info.bus_flags = rad_bus_flags;

	drm_display_info_set_bus_formats(&connector->display_info,
					 rad_bus_formats,
					 ARRAY_SIZE(rad_bus_formats));
	return 1;
}

static int rad_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct rad_panel *rad = mipi_dsi_get_drvdata(dsi);
	u16 brightness;
	int ret;

	if (!rad->prepared)
		return 0;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	bl->props.brightness = brightness;

	return brightness & 0xff;
}

static int rad_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct rad_panel *rad = mipi_dsi_get_drvdata(dsi);
	int ret = 0;

	if (!rad->prepared)
		return 0;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, bl->props.brightness);
	if (ret < 0)
		return ret;

	if (rad->external_backlight) {
		/* 1.2.2 used a global one-shot; keep the 1s delay per device. */
		if (!rad->backlight_delay_done) {
			rad->backlight_delay_done = true;
			msleep(1000);
		}
		/* Do not undo blanking while disable is invoking this callback. */
		rad->external_backlight->props.power = bl->props.power;
		rad->external_backlight->props.fb_blank = bl->props.fb_blank;
		rad->external_backlight->props.state = bl->props.state;
		rad->external_backlight->props.brightness =
			min(bl->props.brightness,
			    rad->external_backlight->props.max_brightness);
		return backlight_update_status(rad->external_backlight);
	}
	return 0;
}

static const struct backlight_ops rad_bl_ops = {
	.update_status = rad_bl_update_status,
	.get_brightness = rad_bl_get_brightness,
};

static const struct drm_panel_funcs rad_panel_funcs = {
	.prepare = rad_panel_prepare,
	.unprepare = rad_panel_unprepare,
	.enable = rad_panel_enable,
	.disable = rad_panel_disable,
	.get_modes = rad_panel_get_modes,
};

static const char * const rad_supply_names[] = {
	"v3p3",
	"v1p8",
};

static int rad_init_regulators(struct rad_panel *rad)
{
	struct device *dev = &rad->dsi->dev;
	int i;

	rad->num_supplies = ARRAY_SIZE(rad_supply_names);
	rad->supplies = devm_kcalloc(dev, rad->num_supplies,
				     sizeof(*rad->supplies), GFP_KERNEL);
	if (!rad->supplies)
		return -ENOMEM;

	for (i = 0; i < rad->num_supplies; i++)
		rad->supplies[i].supply = rad_supply_names[i];

	return devm_regulator_bulk_get(dev, rad->num_supplies, rad->supplies);
};

static const struct rad_platform_data rad_rm67191 = {
	.enable = &rm67191_enable,
	.mode = &default_mode,
};

static const struct rad_platform_data rad_rm67199 = {
	.enable = &rm67199_enable,
	.mode = &default_mode,
};

static const struct rad_platform_data rad_emb03 = {
	.enable = &emb03_enable,
	.mode = &emb03_mode,
	.emb03 = true,
};

static const struct of_device_id rad_of_match[] = {
	{ .compatible = "factory48,emb03-rm67191", .data = &rad_emb03 },
	{ .compatible = "polyhex,emb03-rm67191", .data = &rad_emb03 },
	{ .compatible = "raydium,rm67191", .data = &rad_rm67191 },
	{ .compatible = "raydium,rm67199", .data = &rad_rm67199 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rad_of_match);

static void emb03_pwd_off(void *data)
{
	struct rad_panel *rad = data;

	gpiod_set_value_cansleep(rad->pwd, 0);
}

static int rad_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct of_device_id *of_id = of_match_device(rad_of_match, dev);
	struct device_node *np = dev->of_node;
	struct rad_panel *panel;
	struct backlight_properties bl_props;
	int ret;
	u32 video_mode;

	if (!of_id || !of_id->data)
		return -ENODEV;

	panel = devm_kzalloc(&dsi->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, panel);

	panel->dsi = dsi;
	panel->pdata = of_id->data;

	if (panel->pdata->emb03) {
		/* Claim before any GPIO changes; a second provider must fail closed. */
		ret = emb03_panel_claim(panel);
		if (ret)
			return ret;
		ret = devm_add_action_or_reset(dev, emb03_panel_release, panel);
		if (ret)
			return ret;
		/* Managed reference and deferred probe, unlike vendor's raw lookup. */
		panel->external_backlight = devm_of_find_backlight(dev);
		if (IS_ERR(panel->external_backlight))
			return PTR_ERR(panel->external_backlight);
	}

	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags =  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO;

	ret = of_property_read_u32(np, "video-mode", &video_mode);
	if (!ret) {
		switch (video_mode) {
		case 0:
			/* burst mode */
			dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_BURST;
			break;
		case 1:
			/* non-burst mode with sync event */
			break;
		case 2:
			/* non-burst mode with sync pulse */
			dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
			break;
		default:
			dev_warn(dev, "invalid video mode %d\n", video_mode);
			break;
		}
	}

	ret = of_property_read_u32(np, "dsi-lanes", &dsi->lanes);
	if (ret) {
		dev_err(dev, "Failed to get dsi-lanes property (%d)\n", ret);
		return ret;
	}

	if (panel->pdata->emb03) {
		/* Vendor probe flags 0x17: OUT_HIGH | NONEXCLUSIVE. */
		panel->pwd = devm_gpiod_get_optional(dev, "pwd",
				GPIOD_OUT_HIGH | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
		if (IS_ERR(panel->pwd))
			return PTR_ERR(panel->pwd);
		gpiod_set_value_cansleep(panel->pwd, 1);
		ret = devm_add_action_or_reset(dev, emb03_pwd_off, panel);
		if (ret)
			return ret;
	}

	panel->reset = devm_gpiod_get_optional(dev, "reset",
					       GPIOD_OUT_LOW |
					       GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(panel->reset)) {
		ret = PTR_ERR(panel->reset);
		dev_err(dev, "Failed to get reset gpio (%d)\n", ret);
		return ret;
	}
	gpiod_set_value_cansleep(panel->reset, 1);

	memset(&bl_props, 0, sizeof(bl_props));
	bl_props.type = BACKLIGHT_RAW;
	bl_props.brightness = 255;
	bl_props.max_brightness = 255;

	panel->backlight = devm_backlight_device_register(dev, dev_name(dev),
							  dev, dsi, &rad_bl_ops,
							  &bl_props);
	if (IS_ERR(panel->backlight)) {
		ret = PTR_ERR(panel->backlight);
		dev_err(dev, "Failed to register backlight (%d)\n", ret);
		return ret;
	}

	ret = rad_init_regulators(panel);
	if (ret)
		return ret;

	drm_panel_init(&panel->panel, dev, &rad_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	dev_set_drvdata(dev, panel);

	drm_panel_add(&panel->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&panel->panel);

	return ret;
}

static int rad_panel_remove(struct mipi_dsi_device *dsi)
{
	struct rad_panel *rad = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;
	int ret;

	/* Quiesce before detach so DCS transfers still have a host. */
	if (rad->pdata->emb03) {
		ret = rad_panel_disable(&rad->panel);
		if (ret)
			dev_warn(dev, "EMB03 disable on remove: %d\n", ret);
		ret = rad_panel_unprepare(&rad->panel);
		if (ret)
			dev_warn(dev, "EMB03 unprepare on remove: %d\n", ret);
		gpiod_set_value_cansleep(rad->pwd, 0);
	}

	ret = mipi_dsi_detach(dsi);
	if (ret)
		dev_err(dev, "Failed to detach from host (%d)\n", ret);

	drm_panel_remove(&rad->panel);

	return 0;
}

static void rad_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct rad_panel *rad = mipi_dsi_get_drvdata(dsi);

	rad_panel_disable(&rad->panel);
	rad_panel_unprepare(&rad->panel);
	if (rad->pdata->emb03)
		gpiod_set_value_cansleep(rad->pwd, 0);
}

static struct mipi_dsi_driver rad_panel_driver = {
	.driver = {
		.name = "panel-raydium-rm67191",
		.of_match_table = rad_of_match,
	},
	.probe = rad_panel_probe,
	.remove = rad_panel_remove,
	.shutdown = rad_panel_shutdown,
};
module_mipi_dsi_driver(rad_panel_driver);

MODULE_AUTHOR("Robert Chiras <robert.chiras@nxp.com>");
MODULE_DESCRIPTION("DRM Driver for Raydium RM67191 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
