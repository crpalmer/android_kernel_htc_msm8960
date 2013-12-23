#include <mach/panel_id.h>
#include <linux/gpio.h>
#include "../../../drivers/video/msm/msm_fb.h"
#include "../../../drivers/video/msm/mipi_dsi.h"
#include "mipi_monarudo.h"
#include "mipi_monarudo_cmds.c"

typedef struct dsi_cmd_t {
  struct dsi_cmd_desc *cmds;
  int count;
} dsi_cmd;

static dsi_cmd video_on_c;
static dsi_cmd display_on_c;
static dsi_cmd display_off_c;
static dsi_cmd backlight_c;

static struct mipi_dsi_panel_platform_data *mipi_monarudo_pdata;
static struct i2c_client *blk_pwm_client;
static bool resume_blk = 0;
static bool backlight_gpio_is_on = true;
static unsigned int pwm_min = 13;
static unsigned int pwm_default = 82;
static unsigned int pwm_max = 255;
static int mipi_monarudo_lcd_init(void);

static int monarudo_send_display_cmds(dsi_cmd *cmd, bool clk_ctrl)
{
	int ret = 0;
	struct dcs_cmd_req cmdreq;

	if (cmd == NULL || (cmd->cmds == NULL) || (cmd->count <= 0))
		return 0;

	cmdreq.cmds = cmd->cmds;
	cmdreq.cmds_cnt = cmd->count;
	cmdreq.flags = CMD_REQ_COMMIT;
	if (clk_ctrl)
		cmdreq.flags |= CMD_CLK_CTRL;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	ret = mipi_dsi_cmdlist_put(&cmdreq);
	if (ret < 0)
		pr_err("%s failed (%d)\n", __func__, ret);
	return ret;
}

int mipi_lcd_on = 1;
static int monarudo_lcd_on(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	struct mipi_panel_info *mipi;

	mfd = platform_get_drvdata(pdev);
	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;
	if (mipi_lcd_on)
		return 0;

	mipi = &mfd->panel_info.mipi;
	if (mipi->mode == DSI_VIDEO_MODE)
		monarudo_send_display_cmds(&video_on_c, false);

	mipi_lcd_on = 1;
	return 0;
}

static int monarudo_lcd_off(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;

	if (!mipi_lcd_on)
		return 0;

	mipi_lcd_on = 0;
	resume_blk = 1;

	return 0;
}

static int monarudo_display_on(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	mfd = platform_get_drvdata(pdev);

	/* It needs 120ms when LP to HS for renesas */
	msleep(120);

	monarudo_send_display_cmds(&display_on_c, (mfd && mfd->panel_info.type == MIPI_CMD_PANEL));

	return 0;
}

static int monarudo_display_off(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	mfd = platform_get_drvdata(pdev);

	monarudo_send_display_cmds(&display_off_c, (mfd && mfd->panel_info.type == MIPI_CMD_PANEL));

	return 0;
}

static unsigned char monarudo_shrink_pwm(int val)
{
	unsigned char shrink_br = BRI_SETTING_MAX;

	if (val <= 0) {
		shrink_br = 0;
	} else if (val > 0 && (val < BRI_SETTING_MIN)) {
		shrink_br = pwm_min;
	} else if ((val >= BRI_SETTING_MIN) && (val <= BRI_SETTING_DEF)) {
		shrink_br = (val - BRI_SETTING_MIN) * (pwm_default - pwm_min) /
		(BRI_SETTING_DEF - BRI_SETTING_MIN) + pwm_min;
	} else if (val > BRI_SETTING_DEF && val <= BRI_SETTING_MAX) {
		shrink_br = (val - BRI_SETTING_DEF) * (pwm_max - pwm_default) /
		(BRI_SETTING_MAX - BRI_SETTING_DEF) + pwm_default;
	} else if (val > BRI_SETTING_MAX)
		shrink_br = pwm_max;

	pr_info("brightness orig=%d, transformed=%d\n", val, shrink_br);

	return shrink_br;
}

static void backlight_gpio_enable(bool on)
{
	pr_debug("monarudo's %s: request on=%d currently=%d\n", __func__, on, backlight_gpio_is_on);

	if (on == backlight_gpio_is_on)
		return;

	if (system_rev == XB) {
		gpio_tlmm_config(GPIO_CFG(MBAT_IN_XA_XB, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
		gpio_set_value(MBAT_IN_XA_XB, on ? 1 : 0);
	} else if (system_rev >= XC) {
		pr_debug("monarudo's %s: turning %s backlight for >= XC\n", __func__, on ? "ON" : "OFF");
		gpio_tlmm_config(GPIO_CFG(BL_HW_EN_XC_XD, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
		gpio_set_value(BL_HW_EN_XC_XD, on ? 1 : 0);
		msleep(1);
	}

	backlight_gpio_is_on = on;
}

void backlight_gpio_off(void)
{
	backlight_gpio_enable(false);
}

void backlight_gpio_on(void)
{
	backlight_gpio_enable(true);
}

static void monarudo_set_backlight(struct msm_fb_data_type *mfd)
{
	int rc;

	write_display_brightness[2] = monarudo_shrink_pwm((unsigned char)(mfd->bl_level));

	if (resume_blk) {
		resume_blk = 0;

		backlight_gpio_on();

		rc = i2c_smbus_write_byte_data(blk_pwm_client, 0x10, 0xC5);
		if (rc)
			pr_err("i2c write fail\n");
		rc = i2c_smbus_write_byte_data(blk_pwm_client, 0x19, 0x13);
		if (rc)
			pr_err("i2c write fail\n");
		rc = i2c_smbus_write_byte_data(blk_pwm_client, 0x14, 0xC2);
		if (rc)
			pr_err("i2c write fail\n");
		rc = i2c_smbus_write_byte_data(blk_pwm_client, 0x79, 0xFF);
		if (rc)
			pr_err("i2c write fail\n");
		rc = i2c_smbus_write_byte_data(blk_pwm_client, 0x1D, 0xFA);
		if (rc)
			pr_err("i2c write fail\n");
	}

	monarudo_send_display_cmds(&backlight_c, false);

	if((mfd->bl_level) == 0) {
		pr_debug("%s: disabling backlight\n", __func__);
		backlight_gpio_off();
		resume_blk = 1;
	}

	return;
}

static int __devinit monarudo_lcd_probe(struct platform_device *pdev)
{
	if (pdev->id == 0) {
		mipi_monarudo_pdata = pdev->dev.platform_data;
		return 0;
	}

	msm_fb_add_device(pdev);

	pr_info("%s\n", __func__);
	return 0;
}

static struct platform_driver this_driver = {
	.probe  = monarudo_lcd_probe,
	.driver = {
		.name   = "mipi_monarudo",
	},
};

static struct msm_fb_panel_data monarudo_panel_data = {
	.on     = monarudo_lcd_on,
	.off    = monarudo_lcd_off,
	.set_backlight = monarudo_set_backlight,
	.late_init = monarudo_display_on,
	.early_off = monarudo_display_off,
};

static int ch_used[3];

int mipi_monarudo_device_register(struct msm_panel_info *pinfo,
					u32 channel, u32 panel)
{
	struct platform_device *pdev = NULL;
	int ret;

	if ((channel >= 3) || ch_used[channel])
		return -ENODEV;

	ch_used[channel] = TRUE;

		ret = mipi_monarudo_lcd_init();
	if (ret) {
		pr_err("mipi_monarudo_lcd_init() failed with ret %u\n", ret);
		return ret;
	}

	pdev = platform_device_alloc("mipi_monarudo", (panel << 8)|channel);
	if (!pdev)
		return -ENOMEM;

	monarudo_panel_data.panel_info = *pinfo;

	ret = platform_device_add_data(pdev, &monarudo_panel_data,
		sizeof(monarudo_panel_data));
	if (ret) {
		pr_err("%s: platform_device_add_data failed!\n", __func__);
		goto err_device_put;
	}

	ret = platform_device_add(pdev);
	if (ret) {
		pr_err("%s: platform_device_register failed!\n", __func__);
		goto err_device_put;
	}
	return 0;

err_device_put:
	platform_device_put(pdev);
	return ret;
}

static void mipi_video_sharp_init(void)
{
	video_on_c.cmds = sharp_video_on_cmds;
	video_on_c.count = ARRAY_SIZE(sharp_video_on_cmds);
	display_on_c.cmds = renesas_display_on_cmds;
	display_on_c.count = ARRAY_SIZE(renesas_display_on_cmds);
	display_off_c.cmds = sharp_display_off_cmds;
	display_off_c.count = ARRAY_SIZE(sharp_display_off_cmds);
	backlight_c.cmds = renesas_cmd_backlight_cmds;
	backlight_c.count = ARRAY_SIZE(renesas_cmd_backlight_cmds);
}

static void mipi_video_sony_init(void)
{
	video_on_c.cmds = sony_video_on_cmds;
	video_on_c.count = ARRAY_SIZE(sony_video_on_cmds);
	display_on_c.cmds = renesas_display_on_cmds;
	display_on_c.count = ARRAY_SIZE(renesas_display_on_cmds);
	display_off_c.cmds = sony_display_off_cmds;
	display_off_c.count = ARRAY_SIZE(sony_display_off_cmds);
	backlight_c.cmds = renesas_cmd_backlight_cmds;
	backlight_c.count = ARRAY_SIZE(renesas_cmd_backlight_cmds);
}

static void mipi_monarudo_init_cmds(int ptype)
{
  switch (panel_type)
    {
    case PANEL_ID_DLX_SHARP_RENESAS:
      mipi_video_sharp_init();
      break;
    case PANEL_ID_DLX_SONY_RENESAS:
      mipi_video_sony_init();
      break;
    default:
      pr_err("%s: panel not supported!!\n", __func__);
    }
}

static const struct i2c_device_id pwm_i2c_id[] = {
	{ "pwm_i2c", 0 },
	{ }
};

static int pwm_i2c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int rc;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE | I2C_FUNC_I2C))
		return -ENODEV;

	blk_pwm_client = client;

	return rc;
}

static struct i2c_driver pwm_i2c_driver = {
	.driver = {
		.name = "pwm_i2c",
		.owner = THIS_MODULE,
	},
	.probe = pwm_i2c_probe,
	.remove =  __exit_p( pwm_i2c_remove),
	.id_table =  pwm_i2c_id,
};

static void __exit pwm_i2c_remove(void)
{
	i2c_del_driver(&pwm_i2c_driver);
}

static int mipi_monarudo_lcd_init(void)
{
  int ret;

  ret = i2c_add_driver(&pwm_i2c_driver);
  if (ret)
    pr_err(KERN_ERR "%s: failed to add i2c driver\n", __func__);

  mipi_monarudo_init_cmds(panel_type);

  return platform_driver_register(&this_driver);
}

