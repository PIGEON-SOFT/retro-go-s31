/* ESP32-S31-Korvo-1 + ESP32-S3-LCD-EV-Board-SUB3 target.
 * Display: 4.3-inch 800x480 RGB565 LCD.
 * Touch: GT1151-compatible controller on LCD_I2C.
 */

#define RG_TARGET_NAME             "ESP32-S31-Korvo-1"

/****************************************************************************
 * Storage                                                                  *
 ****************************************************************************/
#define RG_STORAGE_ROOT             "/sd"
#define RG_STORAGE_SDMMC_HOST       SDMMC_HOST_SLOT_0
#define RG_STORAGE_SDMMC_SPEED      10000
#define RG_STORAGE_SDMMC_FORCE_1BIT 1
#define RG_STORAGE_SDMMC_INTERNAL_PULLUPS 1
#define RG_GPIO_SD_POWER            GPIO_NUM_39
#define RG_GPIO_SDMMC_D0            GPIO_NUM_20
#define RG_GPIO_SDMMC_D1            GPIO_NUM_21
#define RG_GPIO_SDMMC_D2            GPIO_NUM_22
#define RG_GPIO_SDMMC_D3            GPIO_NUM_23
#define RG_GPIO_SDMMC_CLK           GPIO_NUM_24
#define RG_GPIO_SDMMC_CMD           GPIO_NUM_25

/****************************************************************************
 * Video                                                                    *
 ****************************************************************************/
#define RG_SCREEN_DRIVER            2
#define RG_SCREEN_WIDTH             800
#define RG_SCREEN_HEIGHT            480
#define RG_SCREEN_BACKLIGHT         100
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 140}
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0}
#define RG_SCREEN_PARTIAL_UPDATES   1

#define RG_RGB_LCD_PIXEL_CLOCK_HZ   (18 * 1000 * 1000)
#define RG_RGB_LCD_HSYNC            1
#define RG_RGB_LCD_HBP              40
#define RG_RGB_LCD_HFP              20
#define RG_RGB_LCD_VSYNC            1
#define RG_RGB_LCD_VBP              10
#define RG_RGB_LCD_VFP              5

#define RG_GPIO_LCD_VSYNC           GPIO_NUM_45
#define RG_GPIO_LCD_HSYNC           GPIO_NUM_44
#define RG_GPIO_LCD_DE              GPIO_NUM_43
#define RG_GPIO_LCD_PCLK            GPIO_NUM_40
#define RG_GPIO_LCD_DISP_EN         GPIO_NUM_NC
#define RG_GPIO_LCD_DATA0           GPIO_NUM_8
#define RG_GPIO_LCD_DATA1           GPIO_NUM_9
#define RG_GPIO_LCD_DATA2           GPIO_NUM_10
#define RG_GPIO_LCD_DATA3           GPIO_NUM_11
#define RG_GPIO_LCD_DATA4           GPIO_NUM_12
#define RG_GPIO_LCD_DATA5           GPIO_NUM_13
#define RG_GPIO_LCD_DATA6           GPIO_NUM_14
#define RG_GPIO_LCD_DATA7           GPIO_NUM_15
#define RG_GPIO_LCD_DATA8           GPIO_NUM_16
#define RG_GPIO_LCD_DATA9           GPIO_NUM_17
#define RG_GPIO_LCD_DATA10          GPIO_NUM_18
#define RG_GPIO_LCD_DATA11          GPIO_NUM_19
#define RG_GPIO_LCD_DATA12          GPIO_NUM_33
#define RG_GPIO_LCD_DATA13          GPIO_NUM_34
#define RG_GPIO_LCD_DATA14          GPIO_NUM_35
#define RG_GPIO_LCD_DATA15          GPIO_NUM_36

/****************************************************************************
 * Touch gamepad                                                            *
 ****************************************************************************/
#define RG_GPIO_I2C_SDA             GPIO_NUM_0
#define RG_GPIO_I2C_SCL             GPIO_NUM_1
#define RG_GAMEPAD_TOUCHSCREEN      1
#define RG_TOUCH_GT911_ADDR         0x5D
#define RG_TOUCH_GAMEPAD_FRAME      0
#define RG_TOUCH_GAMEPAD_VIEW_LEFT  205
#define RG_TOUCH_GAMEPAD_VIEW_TOP   60
#define RG_TOUCH_GAMEPAD_VIEW_WIDTH 390
#define RG_TOUCH_GAMEPAD_VIEW_HEIGHT 300

/****************************************************************************
 * Audio                                                                    *
 ****************************************************************************/
#define RG_AUDIO_USE_INT_DAC        0
#define RG_AUDIO_USE_EXT_DAC        0
#define RG_AUDIO_USE_ES8389         1
#define RG_GPIO_SND_I2C_SDA         GPIO_NUM_0
#define RG_GPIO_SND_I2C_SCL         GPIO_NUM_1
#define RG_GPIO_SND_I2S_MCLK        GPIO_NUM_2
#define RG_GPIO_SND_I2S_BCK         GPIO_NUM_3
#define RG_GPIO_SND_I2S_WS          GPIO_NUM_4
#define RG_GPIO_SND_I2S_DATA        GPIO_NUM_5
#define RG_GPIO_SND_AMP_ENABLE      GPIO_NUM_7

/****************************************************************************
 * Miscellaneous                                                            *
 ****************************************************************************/
#define RG_RECOVERY_BTN             RG_KEY_MENU
#define RG_UPDATER_ENABLE           0
#define RG_CUSTOM_PLATFORM_INIT() \
    gpio_set_direction(RG_GPIO_SD_POWER, GPIO_MODE_OUTPUT); \
    gpio_set_level(RG_GPIO_SD_POWER, 0);
