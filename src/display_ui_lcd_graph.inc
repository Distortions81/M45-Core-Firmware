/* Private implementation fragment included by display_ui.c. */
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static bool lcd_graph_history_valid(void) {
  return g_lcd_graph_history.magic == LCD_GRAPH_MAGIC &&
         g_lcd_graph_history.version == LCD_GRAPH_VERSION &&
         g_lcd_graph_history.count <= LCD_GRAPH_HISTORY_LEN &&
         g_lcd_graph_history.head < LCD_GRAPH_HISTORY_LEN &&
         g_lcd_graph_history.scale_hashrate > 0 &&
         g_lcd_graph_history.scale_share > 0;
}

static void lcd_graph_load_nvs(void) {
  if (g_lcd_graph_nvs_loaded || lcd_graph_history_valid()) {
    g_lcd_graph_nvs_loaded = true;
    return;
  }

  nvs_handle_t nvs;
  if (nvs_open("display", NVS_READONLY, &nvs) != ESP_OK) {
    g_lcd_graph_nvs_loaded = true;
    return;
  }
  size_t size = sizeof(g_lcd_graph_history);
  if (nvs_get_blob(nvs, "lcd_graph", &g_lcd_graph_history, &size) != ESP_OK ||
      size != sizeof(g_lcd_graph_history) ||
      !lcd_graph_history_valid()) {
    memset(&g_lcd_graph_history, 0, sizeof(g_lcd_graph_history));
  } else {
    g_lcd_graph_last_saved_minute = g_lcd_graph_history.last_bucket_minute;
  }
  nvs_close(nvs);
  g_lcd_graph_nvs_loaded = true;
}

static void lcd_graph_save_nvs(int64_t minute) {
  if (!lcd_graph_history_valid() || !g_lcd_graph_dirty) {
    return;
  }
  if (g_lcd_graph_last_saved_minute >= 0 &&
      minute - g_lcd_graph_last_saved_minute < LCD_GRAPH_NVS_SAVE_INTERVAL_MINUTES) {
    return;
  }

  nvs_handle_t nvs;
  if (nvs_open("display", NVS_READWRITE, &nvs) != ESP_OK) {
    return;
  }
  if (nvs_set_blob(nvs, "lcd_graph", &g_lcd_graph_history, sizeof(g_lcd_graph_history)) == ESP_OK &&
      nvs_commit(nvs) == ESP_OK) {
    g_lcd_graph_dirty = false;
    g_lcd_graph_last_saved_minute = minute;
  }
  nvs_close(nvs);
}

static void lcd_graph_reset_history(int64_t minute, uint64_t hashes_total, uint32_t found_shares) {
  memset(&g_lcd_graph_history, 0, sizeof(g_lcd_graph_history));
  g_lcd_graph_history.magic = LCD_GRAPH_MAGIC;
  g_lcd_graph_history.version = LCD_GRAPH_VERSION;
  g_lcd_graph_history.scale_hashrate =
      g_stratum_runtime.hashes_per_second > 1000U ? g_stratum_runtime.hashes_per_second : 1000U;
  g_lcd_graph_history.scale_share = g_stratum_runtime.best_share > 2U ? g_stratum_runtime.best_share : 2U;
  g_lcd_graph_history.last_bucket_minute = minute;
  g_lcd_graph_history.last_bucket_hashes = hashes_total;
  g_lcd_graph_history.last_bucket_shares = found_shares;
}

static void lcd_graph_ensure_history(int64_t minute, uint64_t hashes_total, uint32_t found_shares) {
  lcd_graph_load_nvs();
  if (!lcd_graph_history_valid()) {
    lcd_graph_reset_history(minute, hashes_total, found_shares);
    return;
  }
  if (g_lcd_graph_history.last_bucket_minute < 0 ||
      g_lcd_graph_history.last_bucket_minute > minute) {
    g_lcd_graph_history.last_bucket_minute = minute;
    g_lcd_graph_history.last_bucket_hashes = hashes_total;
    g_lcd_graph_history.last_bucket_shares = found_shares;
  }
}

static uint8_t lcd_graph_quantize_level(uint32_t hashes_per_second) {
  if (hashes_per_second == 0) {
    return 0;
  }
  if (hashes_per_second > g_lcd_graph_history.scale_hashrate) {
    g_lcd_graph_history.scale_hashrate = hashes_per_second;
  }
  const uint32_t target_height = (LCD_GRAPH_HEIGHT * 3U) / 4U;
  uint32_t level =
      ((uint64_t)hashes_per_second * target_height) / g_lcd_graph_history.scale_hashrate;
  if (level > LCD_GRAPH_HEIGHT) {
    level = LCD_GRAPH_HEIGHT;
  }
  return (uint8_t)level;
}

static void lcd_graph_push_level(uint8_t level, uint8_t share_level) {
  g_lcd_graph_history.levels[g_lcd_graph_history.head] = level;
  g_lcd_graph_history.share_levels[g_lcd_graph_history.head] = share_level;
  g_lcd_graph_history.head = (uint16_t)((g_lcd_graph_history.head + 1U) % LCD_GRAPH_HISTORY_LEN);
  if (g_lcd_graph_history.count < LCD_GRAPH_HISTORY_LEN) {
    ++g_lcd_graph_history.count;
  }
}

static bool lcd_update_minute_history(void) {
  const int64_t minute = esp_timer_get_time() / 60000000LL;
  const uint64_t hashes_total = g_stratum_runtime.hashes_total;
  const uint32_t found_shares = g_stratum_runtime.found_shares;
  lcd_graph_ensure_history(minute, hashes_total, found_shares);

  if (g_lcd_graph_history.last_bucket_minute >= minute) {
    return false;
  }

  const int64_t elapsed_minutes = minute - g_lcd_graph_history.last_bucket_minute;
  const uint64_t hash_delta = hashes_total >= g_lcd_graph_history.last_bucket_hashes
                                  ? hashes_total - g_lcd_graph_history.last_bucket_hashes
                                  : 0;
  const uint64_t seconds = elapsed_minutes > 0 ? (uint64_t)elapsed_minutes * 60ULL : 60ULL;
  const uint32_t bucket_hashrate =
      hash_delta / seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)(hash_delta / seconds);
  const uint8_t level = lcd_graph_quantize_level(bucket_hashrate);
  const uint16_t buckets = elapsed_minutes > LCD_GRAPH_HISTORY_LEN
                               ? LCD_GRAPH_HISTORY_LEN
                               : (uint16_t)elapsed_minutes;
  for (uint16_t i = 0; i < buckets; ++i) {
    const int64_t bucket_minute = g_lcd_graph_history.last_bucket_minute + i;
    const uint8_t share_level = graph_quantize_share_level(
        graph_best_share_for_minute(bucket_minute), &g_lcd_graph_history.scale_share, LCD_GRAPH_HEIGHT);
    lcd_graph_push_level(level, share_level);
  }
  g_lcd_graph_history.last_bucket_minute = minute;
  g_lcd_graph_history.last_bucket_hashes = hashes_total;
  g_lcd_graph_history.last_bucket_shares = found_shares;
  g_lcd_graph_dirty = true;
  lcd_graph_save_nvs(minute);
  return true;
}

static uint8_t lcd_graph_level_by_age(uint16_t age) {
  if (age >= g_lcd_graph_history.count) {
    return 0;
  }
  const uint16_t index =
      (uint16_t)((g_lcd_graph_history.head + LCD_GRAPH_HISTORY_LEN - 1U - age) % LCD_GRAPH_HISTORY_LEN);
  return g_lcd_graph_history.levels[index];
}

static uint8_t lcd_graph_share_level_by_age(uint16_t age) {
  if (age >= g_lcd_graph_history.count) {
    return 0;
  }
  const uint16_t index =
      (uint16_t)((g_lcd_graph_history.head + LCD_GRAPH_HISTORY_LEN - 1U - age) % LCD_GRAPH_HISTORY_LEN);
  return g_lcd_graph_history.share_levels[index];
}

static int lcd_graph_y_from_level(uint8_t level) {
  if (level > LCD_GRAPH_HEIGHT) {
    level = LCD_GRAPH_HEIGHT;
  }
  return LCD_GRAPH_BOTTOM - (int)level;
}

static void lcd_pool_host_text(char* out, size_t out_size) {
  const char* pool = g_stratum_runtime.current_pool[0] != '\0'
                         ? g_stratum_runtime.current_pool
                         : (g_stratum.primary_host[0] != '\0' ? g_stratum.primary_host : "Not configured");
  copy_str(out, out_size, pool);
  strip_commas(out);
}

static bool lcd_best_share_text(char* out, size_t out_size) {
  if (g_stratum_runtime.best_share <= 1U) {
    return false;
  }
  snprintf(out, out_size, "BEST %lu", (unsigned long)g_stratum_runtime.best_share);
  return true;
}

static void lcd_format_uptime_compact(char* out, size_t out_size) {
  const uint64_t uptime_m = (uint64_t)((esp_timer_get_time() - g_boot_us) / 60000000ULL);
  if (uptime_m >= 60000ULL) {
    snprintf(out, out_size, "UP 999H+");
  } else if (uptime_m >= 60ULL) {
    snprintf(out, out_size, "UP %lluH%02llu", (unsigned long long)(uptime_m / 60ULL), (unsigned long long)(uptime_m % 60ULL));
  } else {
    snprintf(out, out_size, "UP %lluM", (unsigned long long)uptime_m);
  }
}

static void lcd_draw_fancy_status_line(int y) {
  char ip[24];
  char hashrate[20];
  char pool[80];
  char uptime[20];
  lcd_runtime_ip_text(ip, sizeof(ip));
  oled_hashrate_text(hashrate, sizeof(hashrate));
  lcd_pool_host_text(pool, sizeof(pool));
  lcd_format_uptime_compact(uptime, sizeof(uptime));

  if (y >= 3 && y < 3 + LCD_FONT_HEIGHT) {
    lcd_draw_text_line(y, 6, 3, ip, LCD_RGB565_TEXT, 150);
    const int w = lcd_text_width(hashrate);
    lcd_draw_text_line(y, LCD_WIDTH - w - 6, 3, hashrate, LCD_RGB565_CYAN, 150);
  }
  if (y >= 153 && y < 153 + LCD_FONT_HEIGHT) {
    lcd_draw_text_line(y, 6, 153, pool, LCD_RGB565_TEXT, 210);
    const int w = lcd_text_width(uptime);
    lcd_draw_text_line(y, LCD_WIDTH - w - 6, 153, uptime, LCD_RGB565_MUTED, 120);
  }
}

static void lcd_draw_fancy_graph_line(int y) {
  for (int x = 0; x < LCD_WIDTH; ++x) {
    g_lcd_line[x] = LCD_RGB565_BG;
  }
  if (y >= LCD_GRAPH_TOP && y <= LCD_GRAPH_BOTTOM && ((y - LCD_GRAPH_TOP) % 32) == 0) {
    for (int x = 0; x < LCD_WIDTH; x += 2) {
      g_lcd_line[x] = LCD_RGB565_GRID;
    }
  }

  const uint8_t current_level = lcd_graph_quantize_level(g_stratum_runtime.hashes_per_second);
  const int best_row_center_y = LCD_GRAPH_TOP + 64;
  const int best_text_y = best_row_center_y - (LCD_FONT_HEIGHT / 2);
  if (y >= best_text_y && y < best_text_y + LCD_FONT_HEIGHT) {
    char best_text[28];
    if (lcd_best_share_text(best_text, sizeof(best_text))) {
      const int w = lcd_text_width(best_text);
      const int x = (LCD_WIDTH - w) / 2;
      lcd_draw_text_line(y, x, best_text_y, best_text, LCD_RGB565_GOLD, 160);
    }
  }

  for (uint16_t age = 0; age < LCD_WIDTH; ++age) {
    const int x = (LCD_WIDTH - 1) - (int)age;
    const bool current_minute = age == 0;
    const bool has_history = !current_minute && (age - 1U) < g_lcd_graph_history.count;
    if (!current_minute && !has_history) {
      break;
    }
    const uint8_t level = current_minute ? current_level : lcd_graph_level_by_age((uint16_t)(age - 1U));
    const int bar_y = lcd_graph_y_from_level(level);
    if (y >= bar_y && y <= LCD_GRAPH_BOTTOM) {
      lcd_blend_pixel(x, LCD_RGB565_CYAN);
      if (current_minute && x + 1 < LCD_WIDTH) {
        lcd_blend_pixel(x + 1, LCD_RGB565_CYAN);
      }
    }
    if (y >= bar_y - 1 && y <= bar_y + 1) {
      lcd_blend_pixel(x, LCD_RGB565_CYAN);
      if (x > 0) {
        lcd_blend_pixel(x - 1, LCD_RGB565_CYAN);
      }
      if (x + 1 < LCD_WIDTH) {
        lcd_blend_pixel(x + 1, LCD_RGB565_CYAN);
      }
    }
    const uint8_t share_level =
        has_history ? lcd_graph_share_level_by_age((uint16_t)(age - 1U)) : 0;
    if (share_level > 0) {
      const int dot_y = lcd_graph_y_from_level(share_level);
      if (y >= dot_y - 2 && y <= dot_y + 2) {
        const int dy = y - dot_y;
        const int half = dy == 0 ? 2 : (dy == 1 || dy == -1 ? 2 : 1);
        lcd_draw_hline_segment(y, x - half, x + half, LCD_RGB565_GOLD);
      }
    }
  }
  lcd_draw_fancy_status_line(y);
}

#if APP_LCD_DEBUG_DIRTY_RECTS
static void lcd_debug_outline_rect(lcd_rect_t rect, lcd_rect_t slice, bool text);
#endif

static bool lcd_flush_fancy_rect(lcd_rect_t rect) {
  rect = lcd_rect_clipped(rect.x0, rect.y0, rect.x1, rect.y1);
  if (!lcd_rect_valid(rect)) {
    return true;
  }
  const int width = rect.x1 - rect.x0;
  const int max_rows = (int)(LCD_DIRTY_BUFFER_PIXELS / (size_t)width);
  if (max_rows <= 0) {
    return false;
  }
  for (int slice_y = rect.y0; slice_y < rect.y1; slice_y += max_rows) {
    const int slice_y1 =
        slice_y + max_rows < rect.y1 ? slice_y + max_rows : rect.y1;
    for (int y = slice_y; y < slice_y1; ++y) {
      lcd_draw_fancy_graph_line(y);
      memcpy(&g_lcd_dirty_buffer[(size_t)(y - slice_y) * (size_t)width],
             &g_lcd_line[rect.x0],
             (size_t)width * sizeof(g_lcd_dirty_buffer[0]));
    }
#if APP_LCD_DEBUG_DIRTY_RECTS
    const lcd_rect_t slice = {.x0 = rect.x0, .y0 = slice_y, .x1 = rect.x1, .y1 = slice_y1};
    lcd_debug_outline_rect(rect, slice, false);
#endif
    if (!oled_check(esp_lcd_panel_draw_bitmap(g_oled_panel,
                                              rect.x0,
                                              slice_y,
                                              rect.x1,
                                              slice_y1,
                                              g_lcd_dirty_buffer),
                    "ST7789 draw fancy rect")) {
      g_oled_panel = NULL;
      return false;
    }
    if (xSemaphoreTake(g_lcd_transfer_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
      ESP_LOGW(TAG, "ST7789 fancy rect wait timed out");
      g_oled_panel = NULL;
      return false;
    }
  }
  return true;
}

static void lcd_flush_fancy_runtime_page(void) {
  const bool minute_advanced = lcd_update_minute_history();
  char ip[24];
  char hashrate_text[20];
  char pool[80];
  char uptime[20];
  lcd_runtime_ip_text(ip, sizeof(ip));
  oled_hashrate_text(hashrate_text, sizeof(hashrate_text));
  lcd_pool_host_text(pool, sizeof(pool));
  lcd_format_uptime_compact(uptime, sizeof(uptime));
  const uint32_t hashrate = g_stratum_runtime.hashes_per_second;
  const uint32_t best_share = g_stratum_runtime.best_share;
  const uint8_t current_level = lcd_graph_quantize_level(hashrate);
  const bool mode_changed = g_lcd_last_flushed_page != g_display_page ||
                            g_lcd_last_flushed_light_mode != g_display.light_mode ||
                            g_lcd_last_flushed_flip_vertical != g_display.flip_vertical;
  const bool full = g_lcd_graph_force_full || mode_changed;
  const bool header_dirty = full ||
                            strcmp(g_lcd_graph_last_ip, ip) != 0 ||
                            strcmp(g_lcd_graph_last_hashrate_text, hashrate_text) != 0;
  const bool plot_dirty = full || minute_advanced ||
                          g_lcd_graph_last_current_level != current_level ||
                          g_lcd_graph_last_best_share != best_share ||
                          g_lcd_graph_last_scale_hashrate != g_lcd_graph_history.scale_hashrate ||
                          g_lcd_graph_last_scale_share != g_lcd_graph_history.scale_share;
  const bool footer_dirty = full ||
                            strcmp(g_lcd_graph_last_pool, pool) != 0 ||
                            strcmp(g_lcd_graph_last_uptime, uptime) != 0;

  if (header_dirty &&
      !lcd_flush_fancy_rect((lcd_rect_t){.x0 = 0,
                                         .y0 = LCD_GRAPH_HEADER_Y0,
                                         .x1 = LCD_WIDTH,
                                         .y1 = LCD_GRAPH_HEADER_Y1})) {
    return;
  }
  if (plot_dirty &&
      !lcd_flush_fancy_rect((lcd_rect_t){.x0 = 0,
                                         .y0 = LCD_GRAPH_PLOT_Y0,
                                         .x1 = LCD_WIDTH,
                                         .y1 = LCD_GRAPH_PLOT_Y1})) {
    return;
  }
  if (footer_dirty &&
      !lcd_flush_fancy_rect((lcd_rect_t){.x0 = 0,
                                         .y0 = LCD_GRAPH_FOOTER_Y0,
                                         .x1 = LCD_WIDTH,
                                         .y1 = LCD_GRAPH_FOOTER_Y1})) {
    return;
  }
  if (g_oled_panel == NULL) {
    return;
  }
  lcd_clear_terminal_row_if_flipped();
  g_lcd_previous_rect_count = 0;
  g_lcd_full_refresh_needed = false;
  g_lcd_last_flushed_page = g_display_page;
  g_lcd_last_flushed_light_mode = g_display.light_mode;
  g_lcd_last_flushed_flip_vertical = g_display.flip_vertical;
  g_lcd_graph_force_full = false;
  g_lcd_graph_last_hashrate = hashrate;
  g_lcd_graph_last_best_share = best_share;
  g_lcd_graph_last_scale_hashrate = g_lcd_graph_history.scale_hashrate;
  g_lcd_graph_last_scale_share = g_lcd_graph_history.scale_share;
  g_lcd_graph_last_current_level = current_level;
  copy_str(g_lcd_graph_last_ip, sizeof(g_lcd_graph_last_ip), ip);
  copy_str(g_lcd_graph_last_hashrate_text,
           sizeof(g_lcd_graph_last_hashrate_text),
           hashrate_text);
  copy_str(g_lcd_graph_last_pool, sizeof(g_lcd_graph_last_pool), pool);
  copy_str(g_lcd_graph_last_uptime, sizeof(g_lcd_graph_last_uptime), uptime);
  lcd_text_bg_cache_reset();
}

#endif
