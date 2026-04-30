/* Private implementation fragment included by display_ui.c. */
void oled_update_status(void) {
  if (g_oled_panel == NULL || g_display_sleeping) {
    return;
  }
  oled_apply_display_brightness();
  oled_apply_display_orientation();
  oled_apply_runtime_inversion();
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  if (g_display_page == 1) {
    lcd_flush_fancy_runtime_page();
    return;
  }
  lcd_dirty_begin_frame();
#endif
  memset(g_oled_frame, 0, sizeof(g_oled_frame));
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  memset(g_lcd_text_overlay, 0, sizeof(g_lcd_text_overlay));
#endif

  if (!g_sta_connected && wifi_setup_ap_active()) {
    if (g_runtime_inverted) {
      g_runtime_inverted = false;
      oled_apply_runtime_inversion();
    }
    const int text_left = QR_TILE_SIZE + QR_TEXT_GAP;
    const int text_width = OLED_WIDTH - text_left;
    oled_draw_setup_qr(0, 0);
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
    oled_draw_text_glyph_bounds(text_left, 8, "WIFI", 1, text_width);
    oled_draw_text_glyph_bounds(text_left, 20, g_ap_ssid, 1, text_width);
    oled_draw_text_glyph_bounds(text_left, 34, g_ap_ip_text, 1, text_width);
    oled_draw_text_glyph_bounds(text_left, 50, "SCAN SETUP", 1, text_width);
#else
    oled_draw_text_clipped(text_left, 8, "WIFI", 1, text_width);
    oled_draw_text_clipped(text_left, 20, g_ap_ssid, 1, text_width);
    oled_draw_text_clipped(text_left, 34, g_ap_ip_text, 1, text_width);
    oled_draw_text_clipped(text_left, 50, "SCAN SETUP", 1, text_width);
#endif
  } else if (!g_sta_connected) {
    if (g_runtime_inverted) {
      g_runtime_inverted = false;
      oled_apply_runtime_inversion();
    }
    oled_draw_wifi_wait_page();
  } else if (g_stratum_runtime.enabled && !g_stratum_runtime.connected) {
    if (g_runtime_inverted) {
      g_runtime_inverted = false;
      oled_apply_runtime_inversion();
    }
    oled_draw_pool_wait_page();
  } else {
    oled_update_screensaver_state();
#if !APP_DISPLAY_IDEASPARK_ESP32_19_LCD
    if (g_display_page == 1) {
      oled_draw_graph_page();
    } else
#endif
    {
      oled_draw_runtime_page();
    }
  }
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  lcd_flush_scaled_oled_frame();
#else
  oled_flush_dirty_frame();
#endif
}
