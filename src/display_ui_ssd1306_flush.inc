/* Private implementation fragment included by display_ui.c. */
#if !APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static void oled_flush_dirty_frame(void) {
  if (g_oled_full_refresh_needed) {
    if (!oled_check(esp_lcd_panel_draw_bitmap(g_oled_panel,
                                              0,
                                              0,
                                              OLED_WIDTH,
                                              OLED_HEIGHT,
                                              g_oled_frame),
                    "SSD1306 draw full")) {
      g_oled_panel = NULL;
      return;
    }
    memcpy(g_oled_previous_frame, g_oled_frame, sizeof(g_oled_previous_frame));
    g_oled_full_refresh_needed = false;
    return;
  }

  uint8_t page = 0;
  while (page < OLED_PAGE_COUNT) {
    const size_t offset = (size_t)page * OLED_WIDTH;
    if (memcmp(&g_oled_previous_frame[offset], &g_oled_frame[offset], OLED_WIDTH) == 0) {
      ++page;
      continue;
    }
    const uint8_t start_page = page;
    do {
      ++page;
    } while (page < OLED_PAGE_COUNT &&
             memcmp(&g_oled_previous_frame[(size_t)page * OLED_WIDTH],
                    &g_oled_frame[(size_t)page * OLED_WIDTH],
                    OLED_WIDTH) != 0);

    const size_t start_offset = (size_t)start_page * OLED_WIDTH;
    if (!oled_check(esp_lcd_panel_draw_bitmap(g_oled_panel,
                                              0,
                                              start_page * 8,
                                              OLED_WIDTH,
                                              page * 8,
                                              &g_oled_frame[start_offset]),
                    "SSD1306 draw dirty")) {
      g_oled_panel = NULL;
      return;
    }
    memcpy(&g_oled_previous_frame[start_offset],
           &g_oled_frame[start_offset],
           (size_t)(page - start_page) * OLED_WIDTH);
  }
}
#endif
