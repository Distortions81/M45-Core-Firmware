/* Private implementation fragment included by display_ui.c. */
static bool oled_draw_setup_qr(int x, int y) {
  if (!g_setup_qr_cache.valid) {
    char payload[48];
    snprintf(payload, sizeof(payload), "WIFI:S:%s;;", g_ap_ssid);
    oled_generate_qr(&g_setup_qr_cache, payload, QR_SETUP_VERSION);
  }
  return oled_draw_cached_qr(&g_setup_qr_cache, x, y);
}

static void oled_hashrate_text(char* out, size_t out_size) {
  const uint32_t hashes = g_stratum_runtime.hashes_per_second;
  if (hashes >= 1000) {
    snprintf(out, out_size, "%lu kH/s", (unsigned long)((hashes + 500U) / 1000U));
  } else {
    snprintf(out, out_size, "%lu H/s", (unsigned long)hashes);
  }
}

static void oled_format_short_uptime(char* out, size_t out_size) {
  const uint64_t uptime_m = (uint64_t)((esp_timer_get_time() - g_boot_us) / 60000000ULL);
  if (uptime_m == 0) {
    out[0] = '\0';
    return;
  }
  const uint64_t display_m = (uptime_m / 5ULL) * 5ULL;
  const uint64_t rounded_m = display_m > 0 ? display_m : uptime_m;
  if (rounded_m >= 60000ULL) {
    snprintf(out, out_size, "UP 999H+");
  } else if (rounded_m >= 60ULL) {
    snprintf(out, out_size, "UP %lluH", (unsigned long long)(rounded_m / 60ULL));
  } else {
    snprintf(out, out_size, "UP %lluM", (unsigned long long)rounded_m);
  }
}

static void oled_short_ip_text(char* out, size_t out_size) {
  unsigned a = 0;
  unsigned b = 0;
  unsigned c = 0;
  unsigned d = 0;
  if (sscanf(g_sta_ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
    snprintf(out, out_size, "x.x.%u.%u", c, d);
  } else {
    copy_str(out, out_size, g_sta_ip[0] != '\0' ? g_sta_ip : "NO IP");
  }
}

static uint32_t graph_best_share_for_minute(int64_t minute) {
  if (g_stratum_runtime.share_bucket_minute == minute) {
    return g_stratum_runtime.current_minute_best_share;
  }
  if (g_stratum_runtime.last_completed_share_minute == minute) {
    return g_stratum_runtime.last_completed_minute_best_share;
  }
  return 0;
}

static uint8_t graph_quantize_share_level(uint32_t share, uint32_t* scale, uint8_t height) {
  if (share <= 1U || height == 0) {
    return 0;
  }
  if (*scale < 2U) {
    *scale = 2U;
  }
  if (share > *scale) {
    *scale = share;
  }
  uint32_t level = ((uint64_t)share * height) / *scale;
  if (level == 0) {
    level = 1;
  } else if (level > height) {
    level = height;
  }
  return (uint8_t)level;
}

static void oled_prepare_runtime_ip_cache(void) {
  char ip[20];
  oled_short_ip_text(ip, sizeof(ip));
  text_cache_render(&g_runtime_ip_cache, ip, RUNTIME_TEXT_WIDTH, 4);
}

static void oled_pool_endpoint_lines(char* host, size_t host_size, char* port, size_t port_size) {
  const char* pool = g_stratum_runtime.current_pool[0] != '\0'
                         ? g_stratum_runtime.current_pool
                         : (g_stratum.primary_host[0] != '\0' ? g_stratum.primary_host : "Not configured");
  const uint16_t pool_port =
      g_stratum_runtime.current_port > 0 ? g_stratum_runtime.current_port : g_stratum.primary_port;
  copy_str(host, host_size, pool);
  strip_commas(host);
  if (strcmp(pool, "Not configured") == 0) {
    port[0] = '\0';
  } else {
    snprintf(port, port_size, "port %u", pool_port);
  }
}

void oled_prepare_pool_endpoint_cache(void) {
  char pool[64];
  char port[16];
  oled_pool_endpoint_lines(pool, sizeof(pool), port, sizeof(port));
  text_cache_render(&g_pool_endpoint_cache, pool, OLED_WIDTH - 8, 38 & 7);
}

static void oled_runtime_status_text(char* out, size_t out_size) {
  const int64_t now = esp_timer_get_time();
  const bool disconnected_past_threshold =
      g_stratum_runtime.disconnected_at_us > 0 &&
      now > (int64_t)g_stratum_runtime.disconnected_at_us + 30000000LL;
  if (!g_sta_connected) {
    copy_str(out, out_size, "No WiFi");
  } else if (g_stratum_runtime.connected) {
    copy_str(out,
             out_size,
             g_stratum_runtime.using_backup ? "Secondary"
                                            : (g_stratum_runtime.current_pool[0] != '\0'
                                                   ? g_stratum_runtime.current_pool
                                                   : "Connected"));
  } else if (g_stratum_runtime.enabled) {
    copy_str(out, out_size, disconnected_past_threshold ? "No Pools!" : "Connecting");
  } else {
    copy_str(out, out_size, "Offline");
  }
}

static void oled_split_text(const char* input, char* first, size_t first_size, char* second, size_t second_size, int max_chars) {
  first[0] = '\0';
  second[0] = '\0';
  if (input == NULL) {
    return;
  }
  const int len = (int)strlen(input);
  if (len <= max_chars) {
    copy_str(first, first_size, input);
    return;
  }
  int split_at = -1;
  for (int i = max_chars; i > 0; --i) {
    const char ch = input[i - 1];
    if (ch == '.' || ch == '-' || ch == ':' || ch == '/') {
      split_at = i;
      break;
    }
  }
  if (split_at < 1) {
    split_at = max_chars;
  }
  snprintf(first, first_size, "%.*s", split_at, input);
  copy_str(second, second_size, input + split_at);
}

static void oled_apply_runtime_inversion(void) {
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  return;
#else
  if (g_display_sleeping) {
    return;
  }
  if (g_oled_panel != NULL) {
    const bool inverted = (g_display.light_mode != 0) ^ g_runtime_inverted;
    if (!oled_check(esp_lcd_panel_invert_color(g_oled_panel, inverted), "display invert")) {
      g_oled_panel = NULL;
    }
  }
#endif
}

static void oled_apply_display_orientation(void) {
  if (g_display_sleeping) {
    return;
  }
  if (g_oled_panel != NULL && g_applied_flip_vertical != g_display.flip_vertical) {
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
    const bool mirror_x = g_display.flip_vertical == 0;
    const bool mirror_y = g_display.flip_vertical != 0;
#else
    const bool mirror_x = g_display.flip_vertical != 0;
    const bool mirror_y = g_display.flip_vertical != 0;
#endif
    if (!oled_check(esp_lcd_panel_mirror(g_oled_panel,
                                         mirror_x,
                                         mirror_y),
                    "display mirror")) {
      g_oled_panel = NULL;
      return;
    }
    g_applied_flip_vertical = g_display.flip_vertical;
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
    g_lcd_full_refresh_needed = true;
#else
    g_oled_full_refresh_needed = true;
#endif
  }
}

static void oled_update_screensaver_state(void) {
  if (!g_display.screensaver) {
    if (g_runtime_inverted) {
      g_runtime_inverted = false;
      oled_apply_runtime_inversion();
    }
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
    if (g_runtime_qr_on_right) {
      lcd_text_bg_cache_reset();
      g_lcd_clear_runtime_text_slots = true;
    }
#endif
    g_last_runtime_block[0] = '\0';
    g_runtime_qr_on_right = false;
    g_runtime_swap_sides_on_next_block = false;
    return;
  }
  if (g_stratum_runtime.current_block[0] != '\0' &&
      strcmp(g_last_runtime_block, g_stratum_runtime.current_block) != 0) {
    if (g_last_runtime_block[0] != '\0') {
      g_runtime_inverted = !g_runtime_inverted;
      oled_apply_runtime_inversion();
      if (g_runtime_swap_sides_on_next_block) {
        g_runtime_qr_on_right = !g_runtime_qr_on_right;
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
        lcd_text_bg_cache_reset();
        g_lcd_clear_runtime_text_slots = true;
#endif
      }
      g_runtime_swap_sides_on_next_block = !g_runtime_swap_sides_on_next_block;
    }
    copy_str(g_last_runtime_block, sizeof(g_last_runtime_block), g_stratum_runtime.current_block);
  }
}

static void oled_draw_split_lines(int text_left, int text_width, const char* status, const char* hashrate, const char* uptime) {
  char status1[32];
  char status2[32];
  const int max_chars = text_width / FONT_ADVANCE;
  oled_split_text(status, status1, sizeof(status1), status2, sizeof(status2), max_chars > 1 ? max_chars : 1);
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  char ip[20];
  lcd_runtime_ip_text(ip, sizeof(ip));
  oled_draw_text_glyph_bounds(text_left, 4, ip, 1, text_width);
#else
  if (!g_runtime_ip_cache.valid) {
    oled_prepare_runtime_ip_cache();
  }
  oled_draw_text_cached(&g_runtime_ip_cache, text_left, 4, g_runtime_ip_cache.text, text_width);
#endif
  if (status2[0] != '\0') {
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
    oled_draw_text_glyph_bounds(text_left, 16, status1, 1, text_width);
    oled_draw_text_glyph_bounds(text_left, 28, status2, 1, text_width);
#else
    oled_draw_text_clipped(text_left, 16, status1, 1, text_width);
    oled_draw_text_clipped(text_left, 28, status2, 1, text_width);
#endif
    oled_draw_text_clipped(text_left, 40, hashrate, 1, text_width);
    oled_draw_text_clipped(text_left, 52, uptime, 1, text_width);
  } else {
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
    oled_draw_text_glyph_bounds(text_left, 20, status1, 1, text_width);
#else
    oled_draw_text_clipped(text_left, 20, status1, 1, text_width);
#endif
    oled_draw_text_clipped(text_left, 36, hashrate, 1, text_width);
    oled_draw_text_clipped(text_left, 52, uptime, 1, text_width);
  }
}

static void oled_draw_centered_text(int y, const char* text, int scale) {
  const int width = (int)strlen(text) * FONT_ADVANCE * scale - scale;
  const int x = width < OLED_WIDTH ? (OLED_WIDTH - width) / 2 : 0;
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  oled_draw_text_glyph_bounds(x, y, text, scale, OLED_WIDTH - x);
#else
  oled_draw_text_clipped(x, y, text, scale, OLED_WIDTH - x);
#endif
}

static void oled_draw_wifi_glyph(int center_x, int y, uint8_t phase) {
  static const uint8_t radii[] = {28, 20, 12};
  static const uint8_t half_widths[] = {21, 15, 9};
  const int origin_y = y + 31;
  for (uint8_t arc = 0; arc < 3; ++arc) {
    const bool active = arc <= phase;
    const int radius = radii[arc];
    const int half_width = half_widths[arc];
    for (int dx = -half_width; dx <= half_width; ++dx) {
      const int y_sq = radius * radius - dx * dx;
      if (y_sq < 0) {
        continue;
      }
      int dy = 0;
      while ((dy + 1) * (dy + 1) <= y_sq) {
        ++dy;
      }
      const int py = origin_y - dy;
      oled_set_pixel(center_x + dx, py, true);
      if (active) {
        oled_set_pixel(center_x + dx, py + 1, true);
      }
    }
  }
  oled_set_pixel(center_x - 1, y + 22, true);
  oled_set_pixel(center_x, y + 22, true);
  oled_set_pixel(center_x - 1, y + 23, true);
  oled_set_pixel(center_x, y + 23, true);
}

static void oled_draw_progress_dots(int y, uint8_t phase) {
  const int dot_count = 5;
  const int dot_size = 3;
  const int gap = 5;
  const int total_width = dot_count * dot_size + (dot_count - 1) * gap;
  const int start_x = (OLED_WIDTH - total_width) / 2;
  for (int dot = 0; dot < dot_count; ++dot) {
    const bool active = dot == phase || dot == (phase + dot_count - 1) % dot_count;
    const int x = start_x + dot * (dot_size + gap);
    for (int dy = 0; dy < dot_size; ++dy) {
      for (int dx = 0; dx < dot_size; ++dx) {
        if (active || dx == 1 || dy == 1) {
          oled_set_pixel(x + dx, y + dy, true);
        }
      }
    }
  }
}

static void oled_draw_wifi_wait_page(void) {
  const uint8_t phase = (uint8_t)((esp_timer_get_time() / 1000000LL) % 5);
  if (!wifi_station_started()) {
    oled_draw_wifi_glyph(OLED_WIDTH / 2, 7, phase % 3);
    oled_draw_centered_text(39, "BOOTING", 1);
    oled_draw_progress_dots(53, phase);
    return;
  }
  oled_draw_wifi_glyph(OLED_WIDTH / 2, 4, phase % 3);
  oled_draw_centered_text(34, "CONNECTING", 1);
  if (g_wifi.ssid[0] != '\0') {
    const int ssid_width = (int)strlen(g_wifi.ssid) * FONT_ADVANCE - 1;
    const int ssid_x = ssid_width < OLED_WIDTH ? (OLED_WIDTH - ssid_width) / 2 : 0;
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
    oled_draw_text_glyph_bounds(ssid_x, 45, g_wifi.ssid, 1, OLED_WIDTH - ssid_x);
#else
    oled_draw_text_clipped(ssid_x, 45, g_wifi.ssid, 1, OLED_WIDTH - ssid_x);
#endif
  }
  oled_draw_progress_dots(56, phase);
}

static void oled_draw_pool_wait_page(void) {
  const uint8_t phase = (uint8_t)((esp_timer_get_time() / 1000000LL) % 5);
  const bool reconnecting = g_stratum_runtime.disconnected_at_us > 0;
  oled_draw_centered_text(10, reconnecting ? "RECONNECTING" : "CONNECTING", 1);
  oled_draw_centered_text(23, "TO POOL", 1);
  char pool[64];
  char port[16];
  oled_pool_endpoint_lines(pool, sizeof(pool), port, sizeof(port));
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  const int pool_w = lcd_text_width(pool);
  const int pool_x = pool_w < LCD_WIDTH ? (LCD_WIDTH - pool_w) / 2 : 0;
  oled_draw_text_glyph_bounds(pool_x / LCD_FRAME_SCALE,
                              36,
                              pool,
                              1,
                              OLED_WIDTH - (pool_x / LCD_FRAME_SCALE));
  if (port[0] != '\0') {
    const int port_w = lcd_text_width(port);
    const int port_x = port_w < LCD_WIDTH ? (LCD_WIDTH - port_w) / 2 : 0;
    oled_draw_text_glyph_bounds(port_x / LCD_FRAME_SCALE,
                                49,
                                port,
                                1,
                                OLED_WIDTH - (port_x / LCD_FRAME_SCALE));
  }
#else
  if (!g_pool_endpoint_cache.valid) {
    oled_prepare_pool_endpoint_cache();
  }
  oled_draw_text_cached(&g_pool_endpoint_cache, 4, 38, g_pool_endpoint_cache.text, OLED_WIDTH - 8);
  if (port[0] != '\0') {
    oled_draw_centered_text(49, port, 1);
  }
#endif
  oled_draw_progress_dots(55, phase);
}

static void oled_draw_runtime_page(void) {
  char status[64];
  char hashrate[20];
  char uptime[16];
  oled_runtime_status_text(status, sizeof(status));
  oled_hashrate_text(hashrate, sizeof(hashrate));
  oled_format_short_uptime(uptime, sizeof(uptime));

  const bool qr_on_right = g_display.screensaver && g_runtime_qr_on_right;
  const int qr_x = qr_on_right ? OLED_WIDTH - QR_TILE_SIZE : 0;
  const int text_left = qr_on_right ? 0 : QR_TILE_SIZE + QR_TEXT_GAP;
  const int text_width = qr_on_right ? qr_x - QR_TEXT_GAP : OLED_WIDTH - text_left;

#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  if (g_lcd_clear_runtime_text_slots) {
    lcd_mark_runtime_text_slot(0, OLED_WIDTH - QR_TILE_SIZE - QR_TEXT_GAP);
    lcd_mark_runtime_text_slot(QR_TILE_SIZE + QR_TEXT_GAP,
                               OLED_WIDTH - QR_TILE_SIZE - QR_TEXT_GAP);
    g_lcd_clear_runtime_text_slots = false;
  }
#endif

  if (!g_runtime_qr_cache.valid) {
    oled_prepare_runtime_qr();
  }
  oled_draw_cached_qr(&g_runtime_qr_cache, qr_x, 0);
  oled_draw_split_lines(text_left, text_width, status, hashrate, uptime);
}

static bool block_alert_display_active(int64_t now) {
  (void)now;
  if (!g_system.block_alerts ||
      g_stratum_runtime.found_blocks == 0 ||
      g_stratum_runtime.found_blocks <= g_block_alert_dismissed_blocks ||
      g_stratum_runtime.last_found_block_us == 0) {
    return false;
  }
  return true;
}

static void block_alert_hash_label(char* out, size_t out_size) {
  const size_t len = strlen(g_stratum_runtime.last_found_block_hash);
  if (len >= 8) {
    snprintf(out, out_size, "HASH ..%s", g_stratum_runtime.last_found_block_hash + len - 8);
  } else {
    snprintf(out, out_size, "COUNT %lu", (unsigned long)g_stratum_runtime.found_blocks);
  }
}

static void oled_draw_block_found_page(void) {
  char hash[24];
  char diff[24];
  block_alert_hash_label(hash, sizeof(hash));
  snprintf(diff,
           sizeof(diff),
           "DIFF %lu",
           (unsigned long)g_stratum_runtime.last_found_block_share_difficulty);
  oled_draw_centered_text(5, "BLOCK", 2);
  oled_draw_centered_text(23, "FOUND", 2);
  oled_draw_centered_text(45, hash, 1);
  oled_draw_centered_text(56, diff, 1);
}

#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static uint16_t lcd_block_alert_background(int64_t now) {
  static const uint16_t colors[] = {
      LCD_RGB565_ALERT_RED,
      LCD_RGB565_ALERT_YELLOW,
      LCD_RGB565_ALERT_GREEN,
      LCD_RGB565_ALERT_CYAN,
      LCD_RGB565_ALERT_BLUE,
      LCD_RGB565_ALERT_PURPLE,
  };
  const uint32_t phase = (uint32_t)((now / 1000000LL) % (int64_t)(sizeof(colors) / sizeof(colors[0])));
  return colors[phase];
}

static void lcd_draw_centered_alert_text_line(int line_y,
                                              int y,
                                              const char* text,
                                              uint16_t color) {
  const int width = lcd_text_width(text);
  const int x = width < LCD_WIDTH ? (LCD_WIDTH - width) / 2 : 0;
  lcd_draw_text_line(line_y, x, y, text, color, LCD_WIDTH - x);
}

static int lcd_alert_text_scale(const char* text, int max_scale) {
  const int width = lcd_text_width(text);
  const int available = LCD_WIDTH - 16;
  int scale = max_scale;
  while (scale > 1 && width * scale > available) {
    --scale;
  }
  return scale;
}

static void lcd_draw_scaled_alert_text_line(int line_y,
                                            int y,
                                            const char* text,
                                            uint16_t color,
                                            int scale) {
  if (scale <= 1) {
    lcd_draw_centered_alert_text_line(line_y, y, text, color);
    return;
  }
  const int height = LCD_FONT_HEIGHT * scale;
  const uint16_t outline_color = g_display.light_mode != 0 ? LCD_RGB565_WHITE : LCD_RGB565_BLACK;
  const bool outline = g_display.text_outline != 0 && color != outline_color;
  const int outline_pad = outline ? 1 : 0;
  if (line_y < y - outline_pad || line_y >= y + height + outline_pad) {
    return;
  }
  const int width = lcd_text_width(text) * scale;
  const int end_x = LCD_WIDTH - 8;
  int cursor_x = width < LCD_WIDTH ? (LCD_WIDTH - width) / 2 : 0;
  const int row = line_y >= y ? (line_y - y) / scale : -1;
  while (*text != '\0' && cursor_x < end_x) {
    const unsigned char ch = (unsigned char)*text++;
    if (ch >= LCD_FONT_FIRST_CHAR && ch < LCD_FONT_FIRST_CHAR + LCD_FONT_CHAR_COUNT) {
      const int glyph = ch - LCD_FONT_FIRST_CHAR;
      const int glyph_width = g_lcd_font_widths[glyph];
      if (cursor_x + glyph_width * scale > end_x) {
        break;
      }
      if (outline) {
        for (int source_row = 0; source_row < LCD_FONT_HEIGHT; ++source_row) {
          const int py0 = y + source_row * scale;
          const int py1 = py0 + scale - 1;
          if (line_y < py0 - 1 || line_y > py1 + 1) {
            continue;
          }
          const uint16_t source_bits = g_lcd_font_rows[glyph][source_row];
          for (int col = 0; col < glyph_width; ++col) {
            if ((source_bits & (1U << (LCD_FONT_WIDTH - 1 - col))) == 0) {
              continue;
            }
            const int x0 = cursor_x + col * scale - 1;
            const int x1 = cursor_x + col * scale + scale;
            lcd_draw_hline_segment(line_y, x0, x1, outline_color);
          }
        }
      }
      if (row >= 0 && row < LCD_FONT_HEIGHT) {
        const uint16_t bits = g_lcd_font_rows[glyph][row];
        for (int col = 0; col < glyph_width; ++col) {
          if ((bits & (1U << (LCD_FONT_WIDTH - 1 - col))) == 0) {
            continue;
          }
          for (int dx = 0; dx < scale; ++dx) {
            lcd_blend_pixel(cursor_x + col * scale + dx, color);
          }
        }
      }
      cursor_x += g_lcd_font_advances[glyph] * scale;
    } else {
      cursor_x += (LCD_FONT_WIDTH / 2) * scale;
    }
  }
}

static void lcd_flush_block_found_page(int64_t now) {
  if (g_oled_panel == NULL || g_display_sleeping) {
    return;
  }
  char hash[24];
  char diff[24];
  block_alert_hash_label(hash, sizeof(hash));
  snprintf(diff,
           sizeof(diff),
           "DIFF %lu",
           (unsigned long)g_stratum_runtime.last_found_block_share_difficulty);
  const uint16_t background = lcd_block_alert_background(now);
  const uint16_t foreground = LCD_RGB565_WHITE;
  const int title_scale = lcd_alert_text_scale("FOUND", lcd_alert_text_scale("BLOCK", 3));
  for (int y0 = 0; y0 < LCD_HEIGHT; y0 += LCD_DIRTY_BUFFER_PIXELS / LCD_WIDTH) {
    const int rows = LCD_DIRTY_BUFFER_PIXELS / LCD_WIDTH;
    const int y1 = y0 + rows < LCD_HEIGHT ? y0 + rows : LCD_HEIGHT;
    for (int y = y0; y < y1; ++y) {
      for (int x = 0; x < LCD_WIDTH; ++x) {
        g_lcd_line[x] = background;
      }
      lcd_draw_scaled_alert_text_line(y, 16, "BLOCK", foreground, title_scale);
      lcd_draw_scaled_alert_text_line(y, 66, "FOUND", foreground, title_scale);
      lcd_draw_centered_alert_text_line(y, 123, hash, foreground);
      lcd_draw_centered_alert_text_line(y, 145, diff, foreground);
      memcpy(&g_lcd_dirty_buffer[(size_t)(y - y0) * LCD_WIDTH], g_lcd_line, LCD_WIDTH * sizeof(g_lcd_line[0]));
    }
    if (!oled_check(esp_lcd_panel_draw_bitmap(g_oled_panel, 0, y0, LCD_WIDTH, y1, g_lcd_dirty_buffer),
                    "ST7789 block alert")) {
      g_oled_panel = NULL;
      return;
    }
    if (xSemaphoreTake(g_lcd_transfer_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
      ESP_LOGW(TAG, "ST7789 block alert wait timed out");
      g_oled_panel = NULL;
      return;
    }
  }
  g_lcd_full_refresh_needed = true;
  g_lcd_graph_force_full = true;
}
#endif

#if !APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static uint8_t oled_graph_quantize_level(uint32_t hashes_per_second) {
  if (hashes_per_second == 0) {
    return 0;
  }
  if (hashes_per_second > g_oled_graph_scale_hashrate) {
    g_oled_graph_scale_hashrate = hashes_per_second;
  }
  const uint32_t target_height = (OLED_GRAPH_HEIGHT * 3U) / 4U;
  uint32_t level =
      ((uint64_t)hashes_per_second * target_height) / g_oled_graph_scale_hashrate;
  if (level > OLED_GRAPH_HEIGHT) {
    level = OLED_GRAPH_HEIGHT;
  }
  return (uint8_t)level;
}

static void oled_graph_push_level(uint8_t level, uint8_t share_level) {
  g_oled_graph_levels[g_oled_graph_head] = level;
  g_oled_graph_share_levels[g_oled_graph_head] = share_level;
  g_oled_graph_head = (uint8_t)((g_oled_graph_head + 1U) % OLED_GRAPH_HISTORY_LEN);
  if (g_oled_graph_count < OLED_GRAPH_HISTORY_LEN) {
    ++g_oled_graph_count;
  }
}

static void oled_update_minute_history(void) {
  const int64_t minute = esp_timer_get_time() / 60000000LL;
  const uint64_t hashes_total = g_stratum_runtime.hashes_total;
  const uint32_t found_shares = g_stratum_runtime.found_shares;

  if (g_oled_graph_last_bucket_minute < 0 || g_oled_graph_last_bucket_minute > minute) {
    g_oled_graph_last_bucket_minute = minute;
    g_oled_graph_last_bucket_hashes = hashes_total;
    g_oled_graph_last_bucket_shares = found_shares;
    return;
  }
  if (g_oled_graph_last_bucket_minute >= minute) {
    return;
  }

  const int64_t elapsed_minutes = minute - g_oled_graph_last_bucket_minute;
  const uint64_t hash_delta = hashes_total >= g_oled_graph_last_bucket_hashes
                                  ? hashes_total - g_oled_graph_last_bucket_hashes
                                  : 0;
  const uint64_t seconds = elapsed_minutes > 0 ? (uint64_t)elapsed_minutes * 60ULL : 60ULL;
  const uint32_t bucket_hashrate =
      hash_delta / seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)(hash_delta / seconds);
  const uint8_t level = oled_graph_quantize_level(bucket_hashrate);
  const uint8_t buckets = elapsed_minutes > OLED_GRAPH_HISTORY_LEN
                              ? OLED_GRAPH_HISTORY_LEN
                              : (uint8_t)elapsed_minutes;
  for (uint8_t i = 0; i < buckets; ++i) {
    const int64_t bucket_minute = g_oled_graph_last_bucket_minute + i;
    const uint8_t share_level = graph_quantize_share_level(
        graph_best_share_for_minute(bucket_minute), &g_oled_graph_scale_share, OLED_GRAPH_HEIGHT);
    oled_graph_push_level(level, share_level);
  }
  g_oled_graph_last_bucket_minute = minute;
  g_oled_graph_last_bucket_hashes = hashes_total;
  g_oled_graph_last_bucket_shares = found_shares;
}

static uint8_t oled_graph_level_by_age(uint8_t age) {
  if (age >= g_oled_graph_count) {
    return 0;
  }
  const uint8_t index =
      (uint8_t)((g_oled_graph_head + OLED_GRAPH_HISTORY_LEN - 1U - age) % OLED_GRAPH_HISTORY_LEN);
  return g_oled_graph_levels[index];
}

static uint8_t oled_graph_share_level_by_age(uint8_t age) {
  if (age >= g_oled_graph_count) {
    return 0;
  }
  const uint8_t index =
      (uint8_t)((g_oled_graph_head + OLED_GRAPH_HISTORY_LEN - 1U - age) % OLED_GRAPH_HISTORY_LEN);
  return g_oled_graph_share_levels[index];
}

static int oled_graph_y_from_level(uint8_t level) {
  if (level > OLED_GRAPH_HEIGHT) {
    level = OLED_GRAPH_HEIGHT;
  }
  return OLED_GRAPH_BOTTOM - (int)level;
}

static void oled_invert_hline(int y, int x0, int x1) {
  if (x0 < 0) {
    x0 = 0;
  }
  if (x1 >= OLED_WIDTH) {
    x1 = OLED_WIDTH - 1;
  }
  for (int x = x0; x <= x1; ++x) {
    oled_invert_pixel(x, y);
  }
}

static bool oled_best_share_text(char* out, size_t out_size) {
  if (g_stratum_runtime.best_share <= 1U) {
    return false;
  }
  snprintf(out, out_size, "BEST %lu", (unsigned long)g_stratum_runtime.best_share);
  return true;
}

static void oled_draw_graph_page(void) {
  char hashrate[20];
  char uptime[16];
  char ip[20];
  oled_update_minute_history();
  oled_hashrate_text(hashrate, sizeof(hashrate));
  oled_format_short_uptime(uptime, sizeof(uptime));
  copy_str(ip, sizeof(ip), g_sta_ip[0] != '\0' ? g_sta_ip : "NO IP");

  const int hashrate_w = (int)strlen(hashrate) * FONT_ADVANCE - 1;
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  oled_draw_text_glyph_bounds(0, 2, uptime, 1, OLED_WIDTH);
  oled_draw_text_glyph_bounds(OLED_WIDTH - hashrate_w - 1, 2, hashrate, 1, hashrate_w + 1);
  oled_draw_text_glyph_bounds(0, 56, ip, 1, OLED_WIDTH);
#else
  oled_draw_text_clipped(0, 2, uptime, 1, OLED_WIDTH);
  oled_draw_text_clipped(OLED_WIDTH - hashrate_w - 1, 2, hashrate, 1, hashrate_w + 1);
  oled_draw_text_clipped(0, 56, ip, 1, OLED_WIDTH);
#endif

  for (int y = OLED_GRAPH_TOP; y <= OLED_GRAPH_BOTTOM; y += 11) {
    for (int x = 0; x < OLED_WIDTH; x += 3) {
      oled_set_pixel(x, y, true);
    }
  }

  const uint8_t current_level = oled_graph_quantize_level(g_stratum_runtime.hashes_per_second);
  for (uint8_t age = 0; age < OLED_WIDTH; ++age) {
    const int x = (OLED_WIDTH - 1) - (int)age;
    const bool current_minute = age == 0;
    const bool has_history = !current_minute && (age - 1U) < g_oled_graph_count;
    if (!current_minute && !has_history) {
      break;
    }
    const uint8_t level = current_minute ? current_level : oled_graph_level_by_age((uint8_t)(age - 1U));
    const int bar_y = oled_graph_y_from_level(level);
    for (int y = bar_y; y <= OLED_GRAPH_BOTTOM; ++y) {
      oled_set_pixel(x, y, true);
    }
    const uint8_t share_level =
        has_history ? oled_graph_share_level_by_age((uint8_t)(age - 1U)) : 0;
    if (share_level > 0) {
      const int dot_y = oled_graph_y_from_level(share_level);
      oled_invert_hline(dot_y, x - 1, x + 1);
    }
  }

  char best_text[24];
  if (oled_best_share_text(best_text, sizeof(best_text))) {
    const int w = (int)strlen(best_text) * FONT_ADVANCE - 1;
    const int x = w < OLED_WIDTH ? (OLED_WIDTH - w) / 2 : 0;
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
    oled_draw_text_glyph_bounds(x, 31, best_text, 1, OLED_WIDTH - x);
#else
    oled_draw_text_clipped(x, 31, best_text, 1, OLED_WIDTH - x);
#endif
  }
}
#endif

void oled_next_page(void) {
  g_display_page = (uint8_t)((g_display_page + 1U) % 2U);
}
