// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019, Poly Effects
 * based on ili9881d.c
 * Copyright (C) 2017-2018, Bootlin
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct ili9881d {
	struct drm_panel	panel;
	struct mipi_dsi_device	*dsi;

	struct backlight_device *backlight;
	struct regulator	*power;
	struct gpio_desc	*reset;
};

enum ili9881d_op {
	ILI9881D_SWITCH_PAGE,
	ILI9881D_COMMAND,
};

struct ili9881d_instr {
	enum ili9881d_op	op;

	union arg {
		struct cmd {
			u8	cmd;
			u8	data;
		} cmd;
		u8	page;
	} arg;
};

#define ILI9881D_SWITCH_PAGE_INSTR(_page)	\
	{					\
		.op = ILI9881D_SWITCH_PAGE,	\
		.arg = {			\
			.page = (_page),	\
		},				\
	}

#define ILI9881D_COMMAND_INSTR(_cmd, _data)		\
	{						\
		.op = ILI9881D_COMMAND,		\
		.arg = {				\
			.cmd = {			\
				.cmd = (_cmd),		\
				.data = (_data),	\
			},				\
		},					\
	}


static const struct ili9881d_instr ili9881d_init[] = {
	ILI9881D_SWITCH_PAGE_INSTR(3),
	ILI9881D_COMMAND_INSTR(0x01, 0x00),
	ILI9881D_COMMAND_INSTR(0x02, 0x00),
	ILI9881D_COMMAND_INSTR(0x03, 0x53),
	ILI9881D_COMMAND_INSTR(0x04, 0x13),
	ILI9881D_COMMAND_INSTR(0x05, 0x13),
	ILI9881D_COMMAND_INSTR(0x06, 0x06),
	ILI9881D_COMMAND_INSTR(0x07, 0x00),
	ILI9881D_COMMAND_INSTR(0x08, 0x04),
	ILI9881D_COMMAND_INSTR(0x09, 0x00),
	ILI9881D_COMMAND_INSTR(0x0a, 0x00),
	ILI9881D_COMMAND_INSTR(0x0b, 0x00),
	ILI9881D_COMMAND_INSTR(0x0c, 0x00),
	ILI9881D_COMMAND_INSTR(0x0d, 0x00),
	ILI9881D_COMMAND_INSTR(0x0e, 0x00),
	ILI9881D_COMMAND_INSTR(0x0f, 0x00),
	ILI9881D_COMMAND_INSTR(0x10, 0x00),
	ILI9881D_COMMAND_INSTR(0x11, 0x00),
	ILI9881D_COMMAND_INSTR(0x12, 0x00),
	ILI9881D_COMMAND_INSTR(0x13, 0x00),
	ILI9881D_COMMAND_INSTR(0x14, 0x00),
	ILI9881D_COMMAND_INSTR(0x15, 0x00),
	ILI9881D_COMMAND_INSTR(0x16, 0x00),
	ILI9881D_COMMAND_INSTR(0x17, 0x00),
	ILI9881D_COMMAND_INSTR(0x18, 0x08),
	ILI9881D_COMMAND_INSTR(0x19, 0x00),
	ILI9881D_COMMAND_INSTR(0x1a, 0x00),
	ILI9881D_COMMAND_INSTR(0x1b, 0x00),
	ILI9881D_COMMAND_INSTR(0x1c, 0x00),
	ILI9881D_COMMAND_INSTR(0x1d, 0x00),
	ILI9881D_COMMAND_INSTR(0x1e, 0xC0),
	ILI9881D_COMMAND_INSTR(0x1f, 0x80),
	ILI9881D_COMMAND_INSTR(0x20, 0x04),
	ILI9881D_COMMAND_INSTR(0x21, 0x0B),
	ILI9881D_COMMAND_INSTR(0x22, 0x00),
	ILI9881D_COMMAND_INSTR(0x23, 0x00),
	ILI9881D_COMMAND_INSTR(0x24, 0x00),
	ILI9881D_COMMAND_INSTR(0x25, 0x00),
	ILI9881D_COMMAND_INSTR(0x26, 0x00),
	ILI9881D_COMMAND_INSTR(0x27, 0x00),
	ILI9881D_COMMAND_INSTR(0x28, 0x55),
	ILI9881D_COMMAND_INSTR(0x29, 0x03),
	ILI9881D_COMMAND_INSTR(0x2a, 0x00),
	ILI9881D_COMMAND_INSTR(0x2b, 0x00),
	ILI9881D_COMMAND_INSTR(0x2c, 0x00),
	ILI9881D_COMMAND_INSTR(0x2d, 0x00),
	ILI9881D_COMMAND_INSTR(0x2e, 0x00),
	ILI9881D_COMMAND_INSTR(0x2f, 0x00),
	ILI9881D_COMMAND_INSTR(0x30, 0x00),
	ILI9881D_COMMAND_INSTR(0x31, 0x00),
	ILI9881D_COMMAND_INSTR(0x32, 0x00),
	ILI9881D_COMMAND_INSTR(0x33, 0x00),
	ILI9881D_COMMAND_INSTR(0x34, 0x04),
	ILI9881D_COMMAND_INSTR(0x35, 0x05),
	ILI9881D_COMMAND_INSTR(0x36, 0x05),
	ILI9881D_COMMAND_INSTR(0x37, 0x00),
	ILI9881D_COMMAND_INSTR(0x38, 0x3C),
	ILI9881D_COMMAND_INSTR(0x39, 0x50),
	ILI9881D_COMMAND_INSTR(0x3a, 0x01),
	ILI9881D_COMMAND_INSTR(0x3b, 0x40),
	ILI9881D_COMMAND_INSTR(0x3c, 0x00),
	ILI9881D_COMMAND_INSTR(0x3d, 0x01),
	ILI9881D_COMMAND_INSTR(0x3e, 0x00),
	ILI9881D_COMMAND_INSTR(0x3f, 0x00),
	ILI9881D_COMMAND_INSTR(0x40, 0x50),
	ILI9881D_COMMAND_INSTR(0x41, 0x88),
	ILI9881D_COMMAND_INSTR(0x42, 0x00),
	ILI9881D_COMMAND_INSTR(0x43, 0x00),
	ILI9881D_COMMAND_INSTR(0x44, 0x1F),
	//GIP_2
	ILI9881D_COMMAND_INSTR(0x50, 0x01),
	ILI9881D_COMMAND_INSTR(0x51, 0x23),
	ILI9881D_COMMAND_INSTR(0x52, 0x45),
	ILI9881D_COMMAND_INSTR(0x53, 0x67),
	ILI9881D_COMMAND_INSTR(0x54, 0x89),
	ILI9881D_COMMAND_INSTR(0x55, 0xab),
	ILI9881D_COMMAND_INSTR(0x56, 0x01),
	ILI9881D_COMMAND_INSTR(0x57, 0x23),
	ILI9881D_COMMAND_INSTR(0x58, 0x45),
	ILI9881D_COMMAND_INSTR(0x59, 0x67),
	ILI9881D_COMMAND_INSTR(0x5a, 0x89),
	ILI9881D_COMMAND_INSTR(0x5b, 0xab),
	ILI9881D_COMMAND_INSTR(0x5c, 0xcd),
	ILI9881D_COMMAND_INSTR(0x5d, 0xef),
	//GIP_3
	ILI9881D_COMMAND_INSTR(0x5e, 0x03),
	ILI9881D_COMMAND_INSTR(0x5f, 0x14),
	ILI9881D_COMMAND_INSTR(0x60, 0x15),
	ILI9881D_COMMAND_INSTR(0x61, 0x0C),
	ILI9881D_COMMAND_INSTR(0x62, 0x0D),
	ILI9881D_COMMAND_INSTR(0x63, 0x0E),
	ILI9881D_COMMAND_INSTR(0x64, 0x0F),
	ILI9881D_COMMAND_INSTR(0x65, 0x10),
	ILI9881D_COMMAND_INSTR(0x66, 0x11),
	ILI9881D_COMMAND_INSTR(0x67, 0x08),
	ILI9881D_COMMAND_INSTR(0x68, 0x02),
	ILI9881D_COMMAND_INSTR(0x69, 0x0A),
	ILI9881D_COMMAND_INSTR(0x6a, 0x02),
	ILI9881D_COMMAND_INSTR(0x6b, 0x02),
	ILI9881D_COMMAND_INSTR(0x6c, 0x02),
	ILI9881D_COMMAND_INSTR(0x6d, 0x02),
	ILI9881D_COMMAND_INSTR(0x6e, 0x02),
	ILI9881D_COMMAND_INSTR(0x6f, 0x02),
	ILI9881D_COMMAND_INSTR(0x70, 0x02),
	ILI9881D_COMMAND_INSTR(0x71, 0x02),
	ILI9881D_COMMAND_INSTR(0x72, 0x06),
	ILI9881D_COMMAND_INSTR(0x73, 0x02),
	ILI9881D_COMMAND_INSTR(0x74, 0x02),
	ILI9881D_COMMAND_INSTR(0x75, 0x14),
	ILI9881D_COMMAND_INSTR(0x76, 0x15),
	ILI9881D_COMMAND_INSTR(0x77, 0x11),
	ILI9881D_COMMAND_INSTR(0x78, 0x10),
	ILI9881D_COMMAND_INSTR(0x79, 0x0F),
	ILI9881D_COMMAND_INSTR(0x7a, 0x0E),
	ILI9881D_COMMAND_INSTR(0x7b, 0x0D),
	ILI9881D_COMMAND_INSTR(0x7c, 0x0C),
	ILI9881D_COMMAND_INSTR(0x7d, 0x06),
	ILI9881D_COMMAND_INSTR(0x7e, 0x02),
	ILI9881D_COMMAND_INSTR(0x7f, 0x0A),
	ILI9881D_COMMAND_INSTR(0x80, 0x02),
	ILI9881D_COMMAND_INSTR(0x81, 0x02),
	ILI9881D_COMMAND_INSTR(0x82, 0x02),
	ILI9881D_COMMAND_INSTR(0x83, 0x02),
	ILI9881D_COMMAND_INSTR(0x84, 0x02),
	ILI9881D_COMMAND_INSTR(0x85, 0x02),
	ILI9881D_COMMAND_INSTR(0x86, 0x02),
	ILI9881D_COMMAND_INSTR(0x87, 0x02),
	ILI9881D_COMMAND_INSTR(0x88, 0x08),
	ILI9881D_COMMAND_INSTR(0x89, 0x02),
	ILI9881D_COMMAND_INSTR(0x8A, 0x02),
	//CMD_Page 4
	ILI9881D_SWITCH_PAGE_INSTR(4),
	ILI9881D_COMMAND_INSTR(0x70, 0x00),
	ILI9881D_COMMAND_INSTR(0x71, 0x00),
	ILI9881D_COMMAND_INSTR(0x66, 0xFE),
	ILI9881D_COMMAND_INSTR(0x6F, 0x05),
	ILI9881D_COMMAND_INSTR(0x82, 0x1F),
	ILI9881D_COMMAND_INSTR(0x84, 0x1F),
	ILI9881D_COMMAND_INSTR(0x85, 0x0C),
	ILI9881D_COMMAND_INSTR(0x32, 0xAC),
	ILI9881D_COMMAND_INSTR(0x8C, 0x80),
	ILI9881D_COMMAND_INSTR(0x3C, 0xF5),
	ILI9881D_COMMAND_INSTR(0x3A, 0x24),
	ILI9881D_COMMAND_INSTR(0xB5, 0x02),
	ILI9881D_COMMAND_INSTR(0x31, 0x25),
	ILI9881D_COMMAND_INSTR(0x88, 0x33),
	//CMD_Page 1
	ILI9881D_SWITCH_PAGE_INSTR(1),
	ILI9881D_COMMAND_INSTR(0x22, 0x0A),
	ILI9881D_COMMAND_INSTR(0x31, 0x00),
	ILI9881D_COMMAND_INSTR(0x53, 0x6E),
	ILI9881D_COMMAND_INSTR(0x55, 0x78),
	ILI9881D_COMMAND_INSTR(0x50, 0x6B),
	ILI9881D_COMMAND_INSTR(0x51, 0x6B),
	ILI9881D_COMMAND_INSTR(0x60, 0x20),
	ILI9881D_COMMAND_INSTR(0x61, 0x00),
	ILI9881D_COMMAND_INSTR(0x62, 0x0D),
	ILI9881D_COMMAND_INSTR(0x63, 0x00),
	//Pos Register
	ILI9881D_COMMAND_INSTR(0xA0, 0x00),
	ILI9881D_COMMAND_INSTR(0xA1, 0x11),
	ILI9881D_COMMAND_INSTR(0xA2, 0x1D),
	ILI9881D_COMMAND_INSTR(0xA3, 0x13),
	ILI9881D_COMMAND_INSTR(0xA4, 0x15),
	ILI9881D_COMMAND_INSTR(0xA5, 0x27),
	ILI9881D_COMMAND_INSTR(0xA6, 0x1C),
	ILI9881D_COMMAND_INSTR(0xA7, 0x1E),
	ILI9881D_COMMAND_INSTR(0xA8, 0x7E),
	ILI9881D_COMMAND_INSTR(0xA9, 0x1E),
	ILI9881D_COMMAND_INSTR(0xAA, 0x2A),
	ILI9881D_COMMAND_INSTR(0xAB, 0x72),
	ILI9881D_COMMAND_INSTR(0xAC, 0x1A),
	ILI9881D_COMMAND_INSTR(0xAD, 0x1A),
	ILI9881D_COMMAND_INSTR(0xAE, 0x4D),
	ILI9881D_COMMAND_INSTR(0xAF, 0x23),
	ILI9881D_COMMAND_INSTR(0xB0, 0x29),
	ILI9881D_COMMAND_INSTR(0xB1, 0x4A),
	ILI9881D_COMMAND_INSTR(0xB2, 0x59),
	ILI9881D_COMMAND_INSTR(0xB3, 0x3C),
	//Neg Register
	ILI9881D_COMMAND_INSTR(0xC0, 0x00),
	ILI9881D_COMMAND_INSTR(0xC1, 0x10),
	ILI9881D_COMMAND_INSTR(0xC2, 0x1D),
	ILI9881D_COMMAND_INSTR(0xC3, 0x12),
	ILI9881D_COMMAND_INSTR(0xC4, 0x16),
	ILI9881D_COMMAND_INSTR(0xC5, 0x28),
	ILI9881D_COMMAND_INSTR(0xC6, 0x1B),
	ILI9881D_COMMAND_INSTR(0xC7, 0x1D),
	ILI9881D_COMMAND_INSTR(0xC8, 0x7C),
	ILI9881D_COMMAND_INSTR(0xC9, 0x1E),
	ILI9881D_COMMAND_INSTR(0xCA, 0x29),
	ILI9881D_COMMAND_INSTR(0xCB, 0x71),
	ILI9881D_COMMAND_INSTR(0xCC, 0x1A),
	ILI9881D_COMMAND_INSTR(0xCD, 0x19),
	ILI9881D_COMMAND_INSTR(0xCE, 0x4E),
	ILI9881D_COMMAND_INSTR(0xCF, 0x22),
	ILI9881D_COMMAND_INSTR(0xD0, 0x28),
	ILI9881D_COMMAND_INSTR(0xD1, 0x49),
	ILI9881D_COMMAND_INSTR(0xD2, 0x59),
	ILI9881D_COMMAND_INSTR(0xD3, 0x3C),
//CMD_Page 0
	/* ILI9881D_SWITCH_PAGE_INSTR(0), */
/* LCD_ILI9881D_CMD(0x35); //TE on */
};

static inline struct ili9881d *panel_to_ili9881d(struct drm_panel *panel)
{
	return container_of(panel, struct ili9881d, panel);
}

/*
 * The panel seems to accept some private DCS commands that map
 * directly to registers.
 *
 * It is organised by page, with each page having its own set of
 * registers, and the first page looks like it's holding the standard
 * DCS commands.
 *
 * So before any attempt at sending a command or data, we have to be
 * sure if we're in the right page or not.
 */
static int ili9881d_switch_page(struct ili9881d *ctx, u8 page)
{
	u8 buf[4] = { 0xff, 0x98, 0x81, page };
	int ret;

	ret = mipi_dsi_dcs_write_buffer(ctx->dsi, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	return 0;
}

static int ili9881d_send_cmd_data(struct ili9881d *ctx, u8 cmd, u8 data)
{
	u8 buf[2] = { cmd, data };
	int ret;

	ret = mipi_dsi_dcs_write_buffer(ctx->dsi, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	return 0;
}

static int ili9881d_prepare(struct drm_panel *panel)
{
	/* printk("### ILI9881D PREPARING"); */
	struct ili9881d *ctx = panel_to_ili9881d(panel);
	unsigned int i;
	int ret;

	/* Power the panel */
	ret = regulator_enable(ctx->power);
	if (ret)
		return ret;
	msleep(5);

	/* And reset it */
	gpiod_set_value(ctx->reset, 1);
	msleep(20);

	gpiod_set_value(ctx->reset, 0);
	msleep(20);

	for (i = 0; i < ARRAY_SIZE(ili9881d_init); i++) {
		const struct ili9881d_instr *instr = &ili9881d_init[i];

		if (instr->op == ILI9881D_SWITCH_PAGE)
			ret = ili9881d_switch_page(ctx, instr->arg.page);
		else if (instr->op == ILI9881D_COMMAND)
			ret = ili9881d_send_cmd_data(ctx, instr->arg.cmd.cmd,
						      instr->arg.cmd.data);

		if (ret)
			return ret;
	}

	ret = ili9881d_switch_page(ctx, 0);
	if (ret)
		return ret;

	/* printk("### ILI9881D TEAR ON in prepare"); */
	ret = mipi_dsi_dcs_set_tear_on(ctx->dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret)
		return ret;
	/* printk("### ILI9881D TEAR enabled"); */

	// read values test
	/* printk("### ILI9881D getting pixel format"); */
	/* u8 read_result = 0; */
	/* ret = mipi_dsi_dcs_get_pixel_format(ctx->dsi, &read_result); */
	/* printk("### ILI9881D pixel format %d and %d", read_result, ret); */
	/* ret = mipi_dsi_dcs_get_power_mode(ctx->dsi, &read_result); */
	/* printk("### ILI9881D power mode %d and %d", read_result, ret); */
	//

	ret = mipi_dsi_dcs_exit_sleep_mode(ctx->dsi);
	if (ret)
		return ret;
	/* printk("### ILI9881D PREPARED"); */

	return 0;
}

static int ili9881d_enable(struct drm_panel *panel)
{

	struct ili9881d *ctx = panel_to_ili9881d(panel);

	msleep(120);

	mipi_dsi_dcs_set_display_on(ctx->dsi);
	backlight_enable(ctx->backlight);
	printk("### ILI9881D ENABLED");

	return 0;
}

static int ili9881d_disable(struct drm_panel *panel)
{
	printk("### ILI9881D DISABLED");
	struct ili9881d *ctx = panel_to_ili9881d(panel);

	backlight_disable(ctx->backlight);
	return mipi_dsi_dcs_set_display_off(ctx->dsi);
}

static int ili9881d_unprepare(struct drm_panel *panel)
{
	struct ili9881d *ctx = panel_to_ili9881d(panel);

	/* printk("### ILI9881D UNPREPARE"); */
	mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	regulator_disable(ctx->power);
	gpiod_set_value(ctx->reset, 1);

	return 0;
}

/* #define BST_LEN 256 */
/* #define HBP          40//7 */	
/* #define HFP          24//10 */
/* #define HSPW       4//2 */
/* #define VBP          16//5 */		
/* #define VFP          32//25 */
/* #define VSPW       4//5 */	 

/* 
 * default timings 
 * in idle mode? 
 *
 * VFP 14 
 * VBP 14
 * HBP 5
 *
 */
/* static const struct drm_display_mode bananapi_default_mode = { */
/* 	.clock		= 47000, */
/* 	.vrefresh	= 60, */

/* 	.hdisplay	= 720, */
/* 	.hsync_start	= 720 + 24, */
/* 	.hsync_end	= 720 + 24 + 40, */
/* 	.htotal		= 720 + 24 + 40 + 4, */

/* 	.vdisplay	= 1280, */
/* 	.vsync_start	= 1280 + 32, */
/* 	.vsync_end	= 1280 + 32 + 16, */
/* 	.vtotal		= 1280 + 32 + 16 + 4, */
/* }; */

static const struct drm_display_mode bananapi_default_mode = {
	.clock		= 62000,
	.vrefresh	= 60,

	.hdisplay	= 720,
	.hsync_start	= 720 + 10,
	.hsync_end	= 720 + 10 + 20,
	.htotal		= 720 + 10 + 20 + 30,

	.vdisplay	= 1280,
	.vsync_start	= 1280 + 10,
	.vsync_end	= 1280 + 10 + 10,
	.vtotal		= 1280 + 10 + 10 + 20,
};


static int ili9881d_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct ili9881d *ctx = panel_to_ili9881d(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &bananapi_default_mode);
	if (!mode) {
		dev_err(&ctx->dsi->dev, "failed to add mode %ux%ux@%u\n",
			bananapi_default_mode.hdisplay,
			bananapi_default_mode.vdisplay,
			bananapi_default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	panel->connector->display_info.width_mm = 62;
	panel->connector->display_info.height_mm = 110;

	return 1;
}

static const struct drm_panel_funcs ili9881d_funcs = {
	.prepare	= ili9881d_prepare,
	.unprepare	= ili9881d_unprepare,
	.enable		= ili9881d_enable,
	.disable	= ili9881d_disable,
	.get_modes	= ili9881d_get_modes,
};

static int ili9881d_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device_node *np;
	struct ili9881d *ctx;
	int ret;
	printk("### ILI9881D PROBING");

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = &dsi->dev;
	ctx->panel.funcs = &ili9881d_funcs;

	ctx->power = devm_regulator_get(&dsi->dev, "power");
	if (IS_ERR(ctx->power)) {
		dev_err(&dsi->dev, "Couldn't get our power regulator\n");
		return PTR_ERR(ctx->power);
	}

	/* printk("### ILI9881D PROBE look for reset"); */
	ctx->reset = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset)) {
		dev_err(&dsi->dev, "Couldn't get our reset GPIO\n");
		return PTR_ERR(ctx->reset);
	}

	/* printk("### ILI9881D PROBE look for backlight"); */
	np = of_parse_phandle(dsi->dev.of_node, "backlight", 0);
	if (np) {
		ctx->backlight = of_find_backlight_by_node(np);
		of_node_put(np);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	printk("### ILI9881D PROBE add panel");
	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 4;

	/* printk("### ILI9881D PROBE attaching"); */

	return mipi_dsi_attach(dsi);
}

static int ili9881d_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct ili9881d *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	if (ctx->backlight)
		put_device(&ctx->backlight->dev);

	return 0;
}

static const struct of_device_id ili9881d_of_match[] = {
	{ .compatible = "chance,w500hdc019" },
	{ }
};
MODULE_DEVICE_TABLE(of, ili9881d_of_match);

static struct mipi_dsi_driver ili9881d_dsi_driver = {
	.probe		= ili9881d_dsi_probe,
	.remove		= ili9881d_dsi_remove,
	.driver = {
		.name		= "ili9881d-dsi",
		.of_match_table	= ili9881d_of_match,
	},
};
module_mipi_dsi_driver(ili9881d_dsi_driver);

MODULE_AUTHOR("Lok Davison <loki@polyeffects.com>");
MODULE_DESCRIPTION("Ilitek ILI9881D Controller Driver");
MODULE_LICENSE("GPL v2");
