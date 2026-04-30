/* Private implementation fragment included by display_ui.c. */
static uint8_t oled_clamped_brightness(void) {
  return g_display.brightness_pct <= 100 ? g_display.brightness_pct : 100;
}

#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static bool lcd_set_backlight_duty(uint32_t duty) {
  if (ledc_set_duty(LCD_BACKLIGHT_LEDC_MODE, LCD_BACKLIGHT_LEDC_CHANNEL, duty) != ESP_OK ||
      ledc_update_duty(LCD_BACKLIGHT_LEDC_MODE, LCD_BACKLIGHT_LEDC_CHANNEL) != ESP_OK) {
    ESP_LOGW(TAG, "LCD backlight duty update failed");
    return false;
  }
  return true;
}

static void lcd_configure_backlight_pwm(void) {
  const ledc_timer_config_t timer = {
      .speed_mode = LCD_BACKLIGHT_LEDC_MODE,
      .duty_resolution = LCD_BACKLIGHT_LEDC_RESOLUTION,
      .timer_num = LCD_BACKLIGHT_LEDC_TIMER,
      .freq_hz = LCD_BACKLIGHT_LEDC_FREQUENCY,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  if (ledc_timer_config(&timer) != ESP_OK) {
    ESP_LOGW(TAG, "LCD backlight PWM timer configure failed");
    return;
  }

  const ledc_channel_config_t channel = {
      .gpio_num = LCD_BACKLIGHT_GPIO,
      .speed_mode = LCD_BACKLIGHT_LEDC_MODE,
      .channel = LCD_BACKLIGHT_LEDC_CHANNEL,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = LCD_BACKLIGHT_LEDC_TIMER,
      .duty = 0,
      .hpoint = 0,
  };
  if (ledc_channel_config(&channel) != ESP_OK) {
    ESP_LOGW(TAG, "LCD backlight PWM channel configure failed");
  }
}

static void oled_apply_display_brightness(void) {
  const uint8_t brightness = oled_clamped_brightness();
  if (g_applied_brightness_pct == brightness) {
    return;
  }
  const uint32_t duty = ((uint32_t)brightness * LCD_BACKLIGHT_LEDC_MAX_DUTY) / 100U;
  if (lcd_set_backlight_duty(duty)) {
    g_applied_brightness_pct = brightness;
  }
}
#else
#define OLED_MIN_ACTIVE_CONTRAST 1U

static bool oled_set_ssd1306_contrast(uint8_t contrast) {
  if (g_oled_io_handle == NULL) {
    return false;
  }
  return oled_check(esp_lcd_panel_io_tx_param(g_oled_io_handle, 0x81, &contrast, 1),
                    "SSD1306 contrast");
}

static uint8_t oled_ssd1306_contrast_for_brightness(uint8_t brightness) {
  return (uint8_t)(OLED_MIN_ACTIVE_CONTRAST +
                   (((uint32_t)brightness * (255U - OLED_MIN_ACTIVE_CONTRAST)) / 100U));
}

static bool oled_set_ssd1306_display_on(bool enabled) {
  if (g_oled_panel == NULL) {
    return false;
  }
  return oled_check(esp_lcd_panel_disp_on_off(g_oled_panel, enabled),
                    enabled ? "SSD1306 display on" : "SSD1306 display off");
}

static void oled_apply_display_brightness(void) {
  const uint8_t brightness = oled_clamped_brightness();
  if (g_oled_io_handle == NULL || g_applied_brightness_pct == brightness) {
    return;
  }
  const uint8_t contrast = oled_ssd1306_contrast_for_brightness(brightness);
  if (oled_set_ssd1306_contrast(contrast)) {
    g_applied_brightness_pct = brightness;
  }
}
#endif

bool display_is_sleeping(void) {
  return g_display_sleeping;
}

void display_sleep(void) {
  if (g_display_sleeping) {
    return;
  }
  g_display_sleeping = true;
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  if (lcd_set_backlight_duty(0)) {
    g_applied_brightness_pct = 0;
  }
#else
  if (oled_set_ssd1306_display_on(false) || oled_set_ssd1306_contrast(0)) {
    g_applied_brightness_pct = 0;
  }
#endif
}

void display_wake(void) {
  if (!g_display_sleeping) {
    return;
  }
  g_applied_brightness_pct = 0xff;
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  g_lcd_full_refresh_needed = true;
  g_lcd_graph_force_full = true;
  lcd_text_bg_cache_reset();
  oled_apply_display_brightness();
#else
  oled_apply_display_brightness();
  if (!oled_set_ssd1306_display_on(true)) {
    oled_set_ssd1306_contrast(oled_ssd1306_contrast_for_brightness(oled_clamped_brightness()));
  }
  g_oled_full_refresh_needed = true;
#endif
  g_display_sleeping = false;
}

void start_oled(void) {
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  if (g_lcd_transfer_done == NULL) {
    g_lcd_transfer_done = xSemaphoreCreateBinary();
    if (g_lcd_transfer_done == NULL) {
      ESP_LOGW(TAG, "LCD transfer semaphore create failed");
      return;
    }
  }

  const spi_bus_config_t bus_config = {
      .mosi_io_num = LCD_MOSI_GPIO,
      .miso_io_num = -1,
      .sclk_io_num = LCD_SCLK_GPIO,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = sizeof(g_lcd_dirty_buffer),
  };
  if (!oled_check(spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO), "LCD SPI bus init")) {
    return;
  }

  lcd_configure_backlight_pwm();

  esp_lcd_panel_io_handle_t io_handle = NULL;
  const esp_lcd_panel_io_spi_config_t io_config = {
      .cs_gpio_num = LCD_CS_GPIO,
      .dc_gpio_num = LCD_DC_GPIO,
      .spi_mode = 0,
      .pclk_hz = LCD_PIXEL_CLOCK_HZ,
      .trans_queue_depth = 1,
      .on_color_trans_done = lcd_color_transfer_done,
      .user_ctx = g_lcd_transfer_done,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
  };
  if (!oled_check(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                           &io_config,
                                           &io_handle),
                  "ST7789 panel IO create")) {
    return;
  }

  esp_lcd_panel_handle_t panel = NULL;
  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = LCD_RESET_GPIO,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
      .bits_per_pixel = 16,
  };
  if (!oled_check(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel),
                  "ST7789 panel create")) {
    return;
  }
  if (!oled_check(esp_lcd_panel_reset(panel), "ST7789 reset") ||
      !oled_check(esp_lcd_panel_init(panel), "ST7789 init") ||
      !oled_check(esp_lcd_panel_swap_xy(panel, true), "ST7789 swap axes") ||
      !oled_check(esp_lcd_panel_mirror(panel, true, false), "ST7789 mirror") ||
      !oled_check(esp_lcd_panel_set_gap(panel, APP_LCD_X_GAP, APP_LCD_Y_GAP), "ST7789 gap") ||
      !oled_check(esp_lcd_panel_invert_color(panel, APP_LCD_INVERT_COLOR != 0), "ST7789 invert") ||
      !oled_check(esp_lcd_panel_disp_on_off(panel, true), "ST7789 display on")) {
    return;
  }
  g_oled_panel = panel;
  g_oled_io_handle = io_handle;
  g_runtime_inverted = false;
  g_applied_flip_vertical = 0xff;
  g_applied_brightness_pct = 0xff;
  lcd_graph_load_nvs();
  oled_apply_display_brightness();

  memset(g_oled_frame, 0, sizeof(g_oled_frame));
  ESP_LOGI(TAG, "ST7789 initialized for ideaspark ESP32 1.9 inch LCD");
#else
  const i2c_config_t i2c_config = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = OLED_SDA_GPIO,
      .scl_io_num = OLED_SCL_GPIO,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master = {.clk_speed = APP_OLED_I2C_CLOCK_HZ},
      .clk_flags = 0,
  };
  if (!oled_check(i2c_param_config(OLED_I2C_PORT, &i2c_config), "I2C configure")) {
    return;
  }
  if (!oled_check(i2c_driver_install(OLED_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0), "I2C driver install")) {
    return;
  }

  esp_lcd_panel_io_handle_t io_handle = NULL;
  const esp_lcd_panel_io_i2c_config_t io_config = {
      .dev_addr = OLED_I2C_ADDRESS,
      .control_phase_bytes = 1,
      .dc_bit_offset = 6,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
  };
  if (!oled_check(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)OLED_I2C_PORT,
                                           &io_config,
                                           &io_handle),
                  "SSD1306 panel IO create")) {
    i2c_driver_delete(OLED_I2C_PORT);
    return;
  }

  esp_lcd_panel_handle_t panel = NULL;
  esp_lcd_panel_ssd1306_config_t ssd1306_config = {
      .height = OLED_HEIGHT,
  };
  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = OLED_RESET_GPIO,
      .bits_per_pixel = 1,
      .vendor_config = &ssd1306_config,
  };

  if (!oled_check(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel),
                  "SSD1306 panel create")) {
    return;
  }
  if (!oled_check(esp_lcd_panel_reset(panel), "SSD1306 reset") ||
      !oled_check(esp_lcd_panel_init(panel), "SSD1306 init") ||
      !oled_check(esp_lcd_panel_invert_color(panel, false), "SSD1306 invert off") ||
      !oled_check(esp_lcd_panel_disp_on_off(panel, true), "SSD1306 display on")) {
    return;
  }
  g_oled_panel = panel;
  g_oled_io_handle = io_handle;
  g_runtime_inverted = false;
  g_applied_flip_vertical = 0xff;
  g_applied_brightness_pct = 0xff;
  oled_apply_display_brightness();

  memset(g_oled_frame, 0, sizeof(g_oled_frame));
  memset(g_oled_previous_frame, 0, sizeof(g_oled_previous_frame));
  g_oled_full_refresh_needed = false;
  if (!oled_check(esp_lcd_panel_draw_bitmap(panel, 0, 0, OLED_WIDTH, OLED_HEIGHT, g_oled_frame),
                  "SSD1306 clear")) {
    g_oled_panel = NULL;
    return;
  }
  ESP_LOGI(TAG, "SSD1306 initialized with ESP-IDF esp_lcd");
#endif
}
