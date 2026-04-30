/* Private implementation fragment included by display_ui.c. */
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static bool lcd_color_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t* event_data,
                                    void* user_ctx) {
  (void)panel_io;
  (void)event_data;
  SemaphoreHandle_t done = (SemaphoreHandle_t)user_ctx;
  BaseType_t higher_priority_task_woken = pdFALSE;
  if (done != NULL) {
    xSemaphoreGiveFromISR(done, &higher_priority_task_woken);
  }
  return higher_priority_task_woken == pdTRUE;
}
#endif

#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static bool lcd_rect_valid(lcd_rect_t rect) {
  return rect.x0 < rect.x1 && rect.y0 < rect.y1;
}

static lcd_rect_t lcd_rect_clipped(int x0, int y0, int x1, int y1) {
  if (x0 < 0) {
    x0 = 0;
  }
  if (y0 < 0) {
    y0 = 0;
  }
  if (x1 > LCD_WIDTH) {
    x1 = LCD_WIDTH;
  }
  if (y1 > LCD_HEIGHT) {
    y1 = LCD_HEIGHT;
  }
  return (lcd_rect_t){.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1};
}

static bool lcd_rects_equal(lcd_rect_t a, lcd_rect_t b) {
  return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

static lcd_rect_t lcd_rect_union(lcd_rect_t a, lcd_rect_t b) {
  return (lcd_rect_t){
      .x0 = a.x0 < b.x0 ? a.x0 : b.x0,
      .y0 = a.y0 < b.y0 ? a.y0 : b.y0,
      .x1 = a.x1 > b.x1 ? a.x1 : b.x1,
      .y1 = a.y1 > b.y1 ? a.y1 : b.y1,
  };
}

static size_t lcd_rect_area(lcd_rect_t rect) {
  if (!lcd_rect_valid(rect)) {
    return 0;
  }
  return (size_t)(rect.x1 - rect.x0) * (size_t)(rect.y1 - rect.y0);
}

static void lcd_rect_list_add(lcd_dirty_rect_t* rects,
                              uint8_t* count,
                              lcd_rect_t rect,
                              bool text,
                              bool debug_outline) {
  rect = lcd_rect_clipped(rect.x0, rect.y0, rect.x1, rect.y1);
  if (!lcd_rect_valid(rect)) {
    return;
  }
  for (uint8_t i = 0; i < *count; ++i) {
    if (lcd_rects_equal(rects[i].rect, rect) &&
        rects[i].text == text &&
        rects[i].debug_outline == debug_outline) {
      return;
    }
  }
  const uint8_t max_count =
      rects == g_lcd_flush_rects ? (uint8_t)(LCD_DIRTY_RECT_MAX * 2) : LCD_DIRTY_RECT_MAX;
  if (*count < max_count) {
    rects[(*count)++] =
        (lcd_dirty_rect_t){.rect = rect, .text = text, .debug_outline = debug_outline};
    return;
  }
  g_lcd_full_refresh_needed = true;
}

static void lcd_rect_list_merge(lcd_dirty_rect_t* rects, uint8_t* count) {
  bool merged = true;
  while (merged) {
    merged = false;
    for (uint8_t i = 0; i < *count && !merged; ++i) {
      for (uint8_t j = (uint8_t)(i + 1); j < *count; ++j) {
        if (rects[i].text != rects[j].text ||
            rects[i].debug_outline != rects[j].debug_outline) {
          continue;
        }
        const lcd_rect_t combined = lcd_rect_union(rects[i].rect, rects[j].rect);
        const size_t current_area = lcd_rect_area(rects[i].rect) + lcd_rect_area(rects[j].rect);
        if (lcd_rect_area(combined) > current_area + LCD_DIRTY_MERGE_EXTRA_PIXELS) {
          continue;
        }
        rects[i].rect = combined;
        rects[j] = rects[*count - 1];
        --(*count);
        merged = true;
        break;
      }
    }
  }
}

static const uint16_t* lcd_background_pixels(void) {
  return g_display.light_mode != 0 ? g_lcd_background_light : g_lcd_background_dark;
}

static void lcd_text_bg_cache_reset(void) {
  g_lcd_text_bg_cache_count = 0;
  g_lcd_text_bg_cache_used = 0;
}

static const uint16_t* lcd_text_bg_cache_find(lcd_rect_t rect) {
  for (uint8_t i = 0; i < g_lcd_text_bg_cache_count; ++i) {
    if (lcd_rects_equal(g_lcd_text_bg_caches[i].rect, rect)) {
      return &g_lcd_text_bg_cache_pixels[g_lcd_text_bg_caches[i].pixel_offset];
    }
  }
  return NULL;
}

static void lcd_text_bg_cache_claim(lcd_rect_t rect) {
  rect = lcd_rect_clipped(rect.x0, rect.y0, rect.x1, rect.y1);
  if (!lcd_rect_valid(rect) || lcd_text_bg_cache_find(rect) != NULL) {
    return;
  }
  const size_t width = (size_t)(rect.x1 - rect.x0);
  const size_t height = (size_t)(rect.y1 - rect.y0);
  const size_t pixels = width * height;
  if (g_lcd_text_bg_cache_count >= LCD_TEXT_BG_CACHE_MAX ||
      pixels > LCD_TEXT_BG_CACHE_PIXELS - g_lcd_text_bg_cache_used) {
    return;
  }
  const uint16_t* background = lcd_background_pixels();
  const size_t offset = g_lcd_text_bg_cache_used;
  for (int y = rect.y0; y < rect.y1; ++y) {
    memcpy(&g_lcd_text_bg_cache_pixels[offset + (size_t)(y - rect.y0) * width],
           &background[y * LCD_WIDTH + rect.x0],
           width * sizeof(g_lcd_text_bg_cache_pixels[0]));
  }
  g_lcd_text_bg_caches[g_lcd_text_bg_cache_count++] =
      (lcd_text_bg_cache_t){.rect = rect, .pixel_offset = offset};
  g_lcd_text_bg_cache_used += pixels;
}

static void lcd_mark_current_rect(int x0, int y0, int x1, int y1) {
  lcd_rect_list_add(g_lcd_current_rects,
                    &g_lcd_current_rect_count,
                    (lcd_rect_t){.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1},
                    false,
                    true);
}

static void lcd_mark_clear_rect(int x0, int y0, int x1, int y1) {
  lcd_rect_list_add(g_lcd_current_rects,
                    &g_lcd_current_rect_count,
                    (lcd_rect_t){.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1},
                    false,
                    false);
}

static void lcd_mark_text_rect(int x0, int y0, int x1, int y1, bool cache_background) {
  const lcd_rect_t rect = lcd_rect_clipped(x0, y0, x1, y1);
  lcd_rect_list_add(g_lcd_current_rects, &g_lcd_current_rect_count, rect, true, true);
  if (cache_background) {
    lcd_text_bg_cache_claim(rect);
  }
}

static void lcd_mark_scaled_oled_rect(int x0, int y0, int x1, int y1) {
  lcd_mark_current_rect(LCD_FRAME_X + x0 * LCD_FRAME_SCALE,
                        LCD_FRAME_Y + y0 * LCD_FRAME_SCALE,
                        LCD_FRAME_X + x1 * LCD_FRAME_SCALE,
                        LCD_FRAME_Y + y1 * LCD_FRAME_SCALE);
}

static void lcd_mark_runtime_text_slot(int text_left, int text_width) {
  const int x0 = LCD_FRAME_X + text_left * LCD_FRAME_SCALE;
  const int scaled_x1 = x0 + text_width * LCD_FRAME_SCALE;
  const int x1 = text_left >= QR_TILE_SIZE
                     ? LCD_WIDTH
                     : (scaled_x1 < LCD_WIDTH ? scaled_x1 : LCD_WIDTH);
  const int y0 = LCD_FRAME_Y + 4 * LCD_FRAME_SCALE;
  const int y1 = LCD_FRAME_Y + 52 * LCD_FRAME_SCALE + LCD_FONT_HEIGHT;
  lcd_mark_clear_rect(x0, y0, x1, y1);
}

static void lcd_mark_scaled_oled_pixel(int x, int y) {
  const lcd_rect_t rect = {
      .x0 = LCD_FRAME_X + x * LCD_FRAME_SCALE,
      .y0 = LCD_FRAME_Y + y * LCD_FRAME_SCALE,
      .x1 = LCD_FRAME_X + (x + 1) * LCD_FRAME_SCALE,
      .y1 = LCD_FRAME_Y + (y + 1) * LCD_FRAME_SCALE,
  };
  if (!g_lcd_pixel_rect_valid) {
    g_lcd_pixel_rect = rect;
    g_lcd_pixel_rect_valid = true;
    return;
  }
  g_lcd_pixel_rect = lcd_rect_union(g_lcd_pixel_rect, rect);
}

static void lcd_dirty_begin_frame(void) {
  g_lcd_current_rect_count = 0;
  g_lcd_pixel_rect_valid = false;
}

static void lcd_dirty_finish_frame(void) {
  if (g_lcd_pixel_rect_valid) {
    lcd_rect_list_add(g_lcd_current_rects,
                      &g_lcd_current_rect_count,
                      g_lcd_pixel_rect,
                      false,
                      true);
    g_lcd_pixel_rect_valid = false;
  }
}

static void lcd_dirty_remember_current_frame(void) {
  g_lcd_previous_rect_count = g_lcd_current_rect_count;
  memcpy(g_lcd_previous_rects, g_lcd_current_rects, sizeof(g_lcd_current_rects));
}

static void lcd_text_set_pixel(int x, int y) {
  if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) {
    return;
  }
  g_lcd_text_overlay[x + (y / 8) * LCD_WIDTH] |= (uint8_t)(1U << (y & 7));
}

static void lcd_draw_text_clipped_mode(int x, int y, const char* text, int max_width, bool claim_slot) {
  const int start_x = LCD_FRAME_X + x * LCD_FRAME_SCALE;
  const int start_y = LCD_FRAME_Y + y * LCD_FRAME_SCALE;
  const int scaled_end_x = start_x + max_width * LCD_FRAME_SCALE;
  const int end_x = x >= QR_TILE_SIZE ? LCD_WIDTH : (scaled_end_x < LCD_WIDTH ? scaled_end_x : LCD_WIDTH);
  int cursor_x = start_x;
  const int claim_x0 = cursor_x;
  const int claim_x1 = end_x;
  int glyph_x1 = cursor_x;

  while (*text != '\0' && cursor_x < end_x && cursor_x < LCD_WIDTH) {
    const unsigned char ch = (unsigned char)*text++;
    if (ch >= LCD_FONT_FIRST_CHAR && ch < LCD_FONT_FIRST_CHAR + LCD_FONT_CHAR_COUNT) {
      const int glyph = ch - LCD_FONT_FIRST_CHAR;
      const uint16_t* rows = g_lcd_font_rows[glyph];
      const int width = g_lcd_font_widths[glyph];
      if (cursor_x + width > end_x || cursor_x + width > LCD_WIDTH) {
        break;
      }
      for (int row = 0; row < LCD_FONT_HEIGHT; ++row) {
        const uint16_t bits = rows[row];
        for (int col = 0; col < width; ++col) {
          if ((bits & (1U << (LCD_FONT_WIDTH - 1 - col))) != 0) {
            lcd_text_set_pixel(cursor_x + col, start_y + row);
          }
        }
      }
      if (width > 0) {
        glyph_x1 = cursor_x + width;
      }
      cursor_x += g_lcd_font_advances[glyph];
    } else {
      cursor_x += LCD_FONT_WIDTH / 2;
    }
  }
  if (claim_slot && claim_x1 > claim_x0) {
    lcd_mark_text_rect(claim_x0, start_y, claim_x1, start_y + LCD_FONT_HEIGHT, true);
  } else if (!claim_slot && glyph_x1 > claim_x0) {
    lcd_mark_text_rect(claim_x0, start_y, glyph_x1, start_y + LCD_FONT_HEIGHT, false);
  }
}

static void lcd_draw_text_clipped(int x, int y, const char* text, int max_width) {
  lcd_draw_text_clipped_mode(x, y, text, max_width, true);
}

static void lcd_draw_text_glyph_bounds(int x, int y, const char* text, int max_width) {
  lcd_draw_text_clipped_mode(x, y, text, max_width, false);
}

static int lcd_text_width(const char* text) {
  int width = 0;
  while (*text != '\0') {
    const unsigned char ch = (unsigned char)*text++;
    if (ch >= LCD_FONT_FIRST_CHAR && ch < LCD_FONT_FIRST_CHAR + LCD_FONT_CHAR_COUNT) {
      width += g_lcd_font_advances[ch - LCD_FONT_FIRST_CHAR];
    } else {
      width += LCD_FONT_WIDTH / 2;
    }
  }
  return width > 0 ? width - 1 : 0;
}

static void lcd_blend_pixel(int x, uint16_t color) {
  if (x >= 0 && x < LCD_WIDTH) {
    g_lcd_line[x] = color;
  }
}

static void lcd_draw_hline_segment(int y, int x0, int x1, uint16_t color) {
  (void)y;
  if (x0 < 0) {
    x0 = 0;
  }
  if (x1 >= LCD_WIDTH) {
    x1 = LCD_WIDTH - 1;
  }
  for (int x = x0; x <= x1; ++x) {
    g_lcd_line[x] = color;
  }
}

static void lcd_draw_text_line(int line_y, int x, int y, const char* text, uint16_t color, int max_width) {
  if (line_y < y || line_y >= y + LCD_FONT_HEIGHT || max_width <= 0) {
    return;
  }
  const int row = line_y - y;
  const int end_x = x + max_width;
  int cursor_x = x;
  while (*text != '\0' && cursor_x < end_x && cursor_x < LCD_WIDTH) {
    const unsigned char ch = (unsigned char)*text++;
    if (ch >= LCD_FONT_FIRST_CHAR && ch < LCD_FONT_FIRST_CHAR + LCD_FONT_CHAR_COUNT) {
      const int glyph = ch - LCD_FONT_FIRST_CHAR;
      const uint16_t bits = g_lcd_font_rows[glyph][row];
      const int width = g_lcd_font_widths[glyph];
      if (cursor_x + width > end_x || cursor_x + width > LCD_WIDTH) {
        break;
      }
      for (int col = 0; col < width; ++col) {
        if ((bits & (1U << (LCD_FONT_WIDTH - 1 - col))) != 0) {
          lcd_blend_pixel(cursor_x + col, color);
        }
      }
      cursor_x += g_lcd_font_advances[glyph];
    } else {
      cursor_x += LCD_FONT_WIDTH / 2;
    }
  }
}

static void lcd_runtime_ip_text(char* out, size_t out_size) {
  copy_str(out, out_size, g_sta_ip[0] != '\0' ? g_sta_ip : (g_ap_ip_text[0] != '\0' ? g_ap_ip_text : "NO IP"));
}
#endif
