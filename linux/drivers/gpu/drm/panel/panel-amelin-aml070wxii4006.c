#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

struct aml070_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct backlight_device *backlight;
	struct regulator *supply;
	struct {
		struct gpio_desc *reset;
	} gpios;
};

#define dsi_generic_write_seq(dsi, seq...) do {			\
		static const u8 d[] = { seq };				\
		int ret;						\
		ret = mipi_dsi_generic_write(dsi, d, ARRAY_SIZE(d));	\
		if (ret < 0)						\
			return ret;					\
	} while (0)

static inline struct aml070_panel *panel_to_aml070_panel(struct drm_panel *panel)
{
	return container_of(panel, struct aml070_panel, panel);
}

static int aml070_panel_prepare(struct drm_panel *panel)
{
	struct aml070_panel *ctx = panel_to_aml070_panel(panel);
	int ret;

	ret = regulator_enable(ctx->supply);
	if (ret < 0) {
		DRM_DEV_ERROR(&ctx->dsi->dev, "Failed to enable supply: %d\n", ret);
		return ret;
	}
	gpiod_set_value(ctx->gpios.reset, 1);
	msleep(30);
	gpiod_set_value(ctx->gpios.reset, 0);
	msleep(30);
	gpiod_set_value(ctx->gpios.reset, 1);
	msleep(30);
	return 0;
}

static int aml070_panel_unprepare(struct drm_panel *panel)
{
	struct aml070_panel *ctx = panel_to_aml070_panel(panel);
	gpiod_set_value(ctx->gpios.reset, 0);
	regulator_disable(ctx->supply);
	return 0;
}

static int aml070_init_sequence(struct mipi_dsi_device *dsi)
{
	// enter page1
	dsi_generic_write_seq(dsi, 0xee, 0x50);		// ENTER PAGE1
	dsi_generic_write_seq(dsi, 0xea, 0x85, 0x55);	// write enable
	dsi_generic_write_seq(dsi, 0x30, 0x00);		// bist=1
	dsi_generic_write_seq(dsi, 0x31, 0x00);		// bist=1
	dsi_generic_write_seq(dsi, 0x90, 0x50, 0x15);	// ss_tp location
	dsi_generic_write_seq(dsi, 0x24, 0x20);		// mirror te
	dsi_generic_write_seq(dsi, 0x99, 0x00);		// ss_tp de ndg
	dsi_generic_write_seq(dsi, 0x79, 0x00);		// zigzag
	dsi_generic_write_seq(dsi, 0x95, 0x74);		// column invertion
	dsi_generic_write_seq(dsi, 0x7a, 0x20);
	dsi_generic_write_seq(dsi, 0x97, 0x09);		// sm gip
	dsi_generic_write_seq(dsi, 0x7d, 0x08);
	dsi_generic_write_seq(dsi, 0x56, 0x83);

	// enter page2
	dsi_generic_write_seq(dsi, 0xee, 0x60);		// enter page2
	dsi_generic_write_seq(dsi, 0x30, 0x01);		// 4 LANE
	dsi_generic_write_seq(dsi, 0x27, 0x22);
	dsi_generic_write_seq(dsi, 0x31, 0x0f);
	dsi_generic_write_seq(dsi, 0x32, 0xd9);
	dsi_generic_write_seq(dsi, 0x33, 0xc0);
	dsi_generic_write_seq(dsi, 0x34, 0x1f);
	dsi_generic_write_seq(dsi, 0x35, 0x22);
	dsi_generic_write_seq(dsi, 0x36, 0x00);
	dsi_generic_write_seq(dsi, 0x37, 0x00);
	dsi_generic_write_seq(dsi, 0x3a, 0x24);
	dsi_generic_write_seq(dsi, 0x3b, 0x00);
	dsi_generic_write_seq(dsi, 0x3c, 0x1a);		// VCOM SET 29
	dsi_generic_write_seq(dsi, 0x3d, 0x11);		// vgl
	dsi_generic_write_seq(dsi, 0x3e, 0x93);		// vgh
	dsi_generic_write_seq(dsi, 0x42, 0x64);		// vspr
	dsi_generic_write_seq(dsi, 0x43, 0x64);		// vsnr
	dsi_generic_write_seq(dsi, 0x44, 0x0b);		// vgh
	dsi_generic_write_seq(dsi, 0x46, 0x4e);		// vgl
	dsi_generic_write_seq(dsi, 0x8b, 0x90);		// blkh,1
	dsi_generic_write_seq(dsi, 0x8d, 0x45);
	dsi_generic_write_seq(dsi, 0x91, 0x11);
	dsi_generic_write_seq(dsi, 0x92, 0x11);		// frq_cp1_clk[2:0]
	dsi_generic_write_seq(dsi, 0x93, 0x9f);		// fp7721 power	9f
	dsi_generic_write_seq(dsi, 0x9a, 0x00);		// s_out=800
	dsi_generic_write_seq(dsi, 0x9c, 0x80);		// vlength=1280

	// gamma 2.2  2021/01/19
	dsi_generic_write_seq(dsi, 0x47, 0x0f, 0x24, 0x2c, 0x39, 0x36);	// gamma P0.4.8.12.20 // 0X1E
	dsi_generic_write_seq(dsi, 0x5A, 0x0f, 0x24, 0x2c, 0x39, 0x36);	// gamma n 0.4.8.12.20 // 0X1E
	dsi_generic_write_seq(dsi, 0x4C, 0x4a, 0x40, 0x51, 0x31, 0x2f);	// 28.44.64.96.128
	//dsi_generic_write_seq(dsi, 0x5F, 0x4a, 0x40, 0x51, 0x31, 0x2f);	// 28.44.64.96.128.
	dsi_generic_write_seq(dsi, 0x51, 0x2d, 0x10, 0x25, 0x1f, 0x30);	// 159.191.211.227.235
	//dsi_generic_write_seq(dsi, 0x64, 0x2d, 0x10, 0x25, 0x1f, 0x30);	// 159.191.211.227.235
	dsi_generic_write_seq(dsi, 0x56, 0x37, 0x46, 0x5b, 0x7F);	// 243.247.251.255
	dsi_generic_write_seq(dsi, 0x69, 0x37, 0x46, 0x5b, 0x7F);	// 243.247.251.255
	dsi_generic_write_seq(dsi, 0xee, 0x70);

	// STV0   stv1
	dsi_generic_write_seq(dsi, 0x00, 0x03, 0x07, 0x00, 0x01);
	dsi_generic_write_seq(dsi, 0x04, 0x08, 0x0c, 0x55, 0x01);
	dsi_generic_write_seq(dsi, 0x0c, 0x05, 0x3d);

	// CYC0
	dsi_generic_write_seq(dsi, 0x10, 0x05, 0x08, 0x00, 0x01, 0x05);
	dsi_generic_write_seq(dsi, 0x15, 0x00, 0x15, 0x0d, 0x08, 0x00);
	dsi_generic_write_seq(dsi, 0x29, 0x05, 0x3d);

	// forward scan
	// gip22-gip43=gipl1-gipl22
	dsi_generic_write_seq(dsi, 0x60, 0x3c, 0x3c, 0x07, 0x05, 0x17);
	dsi_generic_write_seq(dsi, 0x65, 0x15, 0x13, 0x11, 0x01, 0x03);
	dsi_generic_write_seq(dsi, 0x6a, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c);
	dsi_generic_write_seq(dsi, 0x6f, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c);
	dsi_generic_write_seq(dsi, 0x74, 0x3C, 0x3c);

	// gip0-gip21=gipr1-gipr22
	dsi_generic_write_seq(dsi, 0x80, 0x3c, 0x3c, 0x06, 0x04, 0x16);
	dsi_generic_write_seq(dsi, 0x85, 0x14, 0x12, 0x10, 0x00, 0x02);
	dsi_generic_write_seq(dsi, 0x8a, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c);
	dsi_generic_write_seq(dsi, 0x8f, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c);
	dsi_generic_write_seq(dsi, 0x94, 0x3c, 0x3c);

	dsi_generic_write_seq(dsi, 0xea, 0x00, 0x00);	// write enable
	dsi_generic_write_seq(dsi, 0xee, 0x00);	// ENTER PAGE0

	return 0;
}

static int aml070_panel_enable(struct drm_panel *panel)
{
	struct aml070_panel *ctx = panel_to_aml070_panel(panel);
	int ret;

	/* Software reset */
	DRM_DEV_DEBUG_DRIVER(&ctx->dsi->dev, "mipi_dsi_dcs_soft_reset");
	ret = mipi_dsi_dcs_soft_reset(ctx->dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(&ctx->dsi->dev, "Failed to sofe reset\n");
		goto out;
	}
	msleep(30);

	/* Init panel */
	aml070_init_sequence(ctx->dsi);

	/* Exit sleep mode */
	DRM_DEV_DEBUG_DRIVER(&ctx->dsi->dev, "mipi_dsi_dcs_exit_sleep_mode");
	ret = mipi_dsi_dcs_exit_sleep_mode(ctx->dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(&ctx->dsi->dev, "Failed to exit sleep mode\n");
		goto out;
	}
	msleep(200);

	DRM_DEV_DEBUG_DRIVER(&ctx->dsi->dev, "mipi_dsi_dcs_set_display_on");
	ret = mipi_dsi_dcs_set_display_on(ctx->dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(&ctx->dsi->dev, "Failed to set display on\n");
		goto out;
	}
	msleep(200);

	ret = backlight_enable(ctx->backlight);
	if (ret)
		goto out;

	return 0;
out:
	mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	return ret;
}

static int aml070_panel_disable(struct drm_panel *panel)
{
	struct aml070_panel *ctx = panel_to_aml070_panel(panel);
	backlight_disable(ctx->backlight);
	return mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
}

/* Default timings */
static const struct drm_display_mode default_mode = {
	.clock		= 67200,
	.hdisplay	= 800,
	.hsync_start	= 800 + 80,
	.hsync_end	= 800 + 80 + 20,
	.htotal		= 800 + 80 + 20 + 20,
	.vdisplay	= 1280,
	.vsync_start	= 1280 + 15,
	.vsync_end	= 1280 + 15 + 6,
	.vtotal		= 1280 + 15 + 6 + 8,
	.width_mm	= 94,
	.height_mm	= 151,
};


static int aml070_panel_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct aml070_panel *ctx = panel_to_aml070_panel(panel);
	struct drm_display_mode *mode;
	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		DRM_DEV_ERROR(&ctx->dsi->dev,
				"Failed to add mode " DRM_MODE_FMT "\n",
				DRM_MODE_ARG(&default_mode));
		return -EINVAL;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.bpc = 8;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_display_info_set_bus_formats(&connector->display_info, &bus_format, 1);

	return 1;
}

static const struct drm_panel_funcs aml070_panel_funcs = {
	.get_modes = aml070_panel_get_modes,
	.prepare   = aml070_panel_prepare,
	.enable    = aml070_panel_enable,
	.disable   = aml070_panel_disable,
	.unprepare = aml070_panel_unprepare,
};

static int aml070_panel_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct aml070_panel *ctx;
	int ret;
	u32 video_mode;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supply = devm_regulator_get(&dsi->dev, "vcc-lcd");
	if (IS_ERR(ctx->supply))
		return PTR_ERR(ctx->supply);

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	drm_panel_init(&ctx->panel, &dsi->dev, &aml070_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	ctx->gpios.reset = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->gpios.reset)) {
		dev_err(&dsi->dev, "Couldn't get our reset GPIO\n");
		return PTR_ERR(ctx->gpios.reset);
	}

	ctx->backlight = devm_of_find_backlight(&dsi->dev);
	if (IS_ERR(ctx->backlight)) {
		dev_err(&dsi->dev, "Couldn't get our backlight\n");
		return PTR_ERR(ctx->backlight);
	}

	drm_panel_add(&ctx->panel);

	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 4;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_LPM;

	ret = of_property_read_u32(dsi->dev.of_node, "video-mode", &video_mode);
	if (!ret) {
		dsi->mode_flags = video_mode;
	}
	dev_info(&dsi->dev, "dsi video mode[0x%lx]\n", dsi->mode_flags);

	return mipi_dsi_attach(dsi);
}

static int aml070_panel_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct aml070_panel *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id aml070_panel_of_match[] = {
	{ .compatible = "amelin,aml070wxii4006" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, aml070_panel_of_match);

static struct mipi_dsi_driver aml070_panel_driver = {
	.probe = aml070_panel_dsi_probe,
	.remove = aml070_panel_dsi_remove,
	.driver = {
		.name = "panel-aml070wxii4006",
		.of_match_table	= aml070_panel_of_match,
	},
};
module_mipi_dsi_driver(aml070_panel_driver);

MODULE_AUTHOR("tronlong");
MODULE_DESCRIPTION("Amelin AML070WXII4006 Panel Driver");
MODULE_LICENSE("GPL");
