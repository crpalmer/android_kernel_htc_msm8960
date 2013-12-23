#ifndef MIPI_MONARUDO_H
#define MIPI_MONARUDO_H

#include <linux/pwm.h>
#include <linux/mfd/pm8xxx/pm8921.h>
#include "../board-monarudo.h"

int mipi_monarudo_device_register(struct msm_panel_info *pinfo,
				  u32 channel, u32 panel);

void backlight_gpio_off(void);
void backlight_gpio_on(void);

#define BRI_SETTING_MIN                 30
#define BRI_SETTING_DEF                 142
#define BRI_SETTING_MAX                 255

#endif

