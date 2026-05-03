/* Private implementation fragment included by display_ui.c. */
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static void lcd_clear_terminal_row_if_flipped(void) {
  if (g_oled_panel == NULL || g_display_sleeping) {
    return;
  }
  if (g_display.flip_vertical == 0) {
    return;
  }
  const bool light_theme = g_display.light_mode != 0;
  const uint16_t color = g_display_page == 1
                             ? LCD_RGB565_BG
                             : (light_theme ? g_lcd_background_light[LCD_WIDTH * (LCD_HEIGHT - 1)]
                                            : g_lcd_background_dark[LCD_WIDTH * (LCD_HEIGHT - 1)]);
  for (int x = 0; x < LCD_WIDTH; ++x) {
    g_lcd_line[x] = color;
  }
  if (!oled_check(esp_lcd_panel_draw_bitmap(g_oled_panel,
                                            0,
                                            LCD_HEIGHT - 1,
                                            LCD_WIDTH,
                                            LCD_HEIGHT,
                                            g_lcd_line),
                  "ST7789 clear terminal row")) {
    g_oled_panel = NULL;
    return;
  }
  if (xSemaphoreTake(g_lcd_transfer_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGW(TAG, "ST7789 terminal row wait timed out");
    g_oled_panel = NULL;
  }
}

static void lcd_render_scaled_rect_line(lcd_rect_t rect,
                                        lcd_rect_t source_rect,
                                        int y,
                                        uint16_t foreground,
                                        const uint16_t* background,
                                        const uint16_t* cached_bg) {
  const size_t width = (size_t)(rect.x1 - rect.x0);
  const size_t line_offset = (size_t)(y - rect.y0) * width;
  uint16_t* line = g_lcd_dirty_buffer + line_offset;
  if (cached_bg != NULL) {
    const size_t source_width = (size_t)(source_rect.x1 - source_rect.x0);
    const size_t source_offset =
        (size_t)(y - source_rect.y0) * source_width + (size_t)(rect.x0 - source_rect.x0);
    memcpy(line, cached_bg + source_offset, width * sizeof(g_lcd_dirty_buffer[0]));
  } else {
    memcpy(line, &background[y * LCD_WIDTH + rect.x0], width * sizeof(g_lcd_dirty_buffer[0]));
  }

  if (y >= LCD_FRAME_Y && y < LCD_FRAME_Y + LCD_FRAME_HEIGHT) {
    const int source_y = (y - LCD_FRAME_Y) / LCD_FRAME_SCALE;
    const int page = source_y / 8;
    const uint8_t bit = (uint8_t)(1U << (source_y & 7));
    int source_x0 = (rect.x0 - LCD_FRAME_X + LCD_FRAME_SCALE - 1) / LCD_FRAME_SCALE;
    int source_x1 = (rect.x1 - LCD_FRAME_X + LCD_FRAME_SCALE - 1) / LCD_FRAME_SCALE;
    if (source_x0 < 0) {
      source_x0 = 0;
    }
    if (source_x1 > OLED_WIDTH) {
      source_x1 = OLED_WIDTH;
    }
    for (int source_x = source_x0; source_x < source_x1; ++source_x) {
      if ((g_oled_frame[source_x + page * OLED_WIDTH] & bit) == 0) {
        continue;
      }
      const int target_x = LCD_FRAME_X + source_x * LCD_FRAME_SCALE;
      if (target_x >= rect.x0 && target_x < rect.x1) {
        line[target_x - rect.x0] = foreground;
      }
      if (target_x + 1 >= rect.x0 && target_x + 1 < rect.x1) {
        line[target_x + 1 - rect.x0] = foreground;
      }
    }
  }

  const uint8_t text_bit = (uint8_t)(1U << (y & 7));
  const int text_page = y / 8;
  const uint16_t outline_color = g_display.light_mode != 0 ? LCD_RGB565_WHITE : LCD_RGB565_BLACK;
  for (int x = rect.x0; x < rect.x1; ++x) {
    if ((g_lcd_text_outline[x + text_page * LCD_WIDTH] & text_bit) != 0) {
      line[x - rect.x0] = outline_color;
    }
    if ((g_lcd_text_overlay[x + text_page * LCD_WIDTH] & text_bit) != 0) {
      line[x - rect.x0] = foreground;
    }
  }
}

#if APP_LCD_DEBUG_DIRTY_RECTS
static void lcd_debug_outline_rect(lcd_rect_t rect, lcd_rect_t slice, bool text) {
  const uint16_t color = text ? LCD_DEBUG_TEXT_RGB565 : LCD_DEBUG_DIRTY_RGB565;
  const int width = slice.x1 - slice.x0;
  const int height = slice.y1 - slice.y0;
  if (width <= 0 || height <= 0) {
    return;
  }
  if (rect.y0 >= slice.y0 && rect.y0 < slice.y1) {
    uint16_t* line = g_lcd_dirty_buffer + (size_t)(rect.y0 - slice.y0) * (size_t)width;
    for (int x = 0; x < width; ++x) {
      line[x] = color;
    }
  }
  if (rect.y1 - 1 >= slice.y0 && rect.y1 - 1 < slice.y1) {
    uint16_t* line = g_lcd_dirty_buffer + (size_t)(rect.y1 - 1 - slice.y0) * (size_t)width;
    for (int x = 0; x < width; ++x) {
      line[x] = color;
    }
  }
  const int left = rect.x0 - slice.x0;
  const int right = rect.x1 - 1 - slice.x0;
  for (int y = 0; y < height; ++y) {
    uint16_t* line = g_lcd_dirty_buffer + (size_t)y * (size_t)width;
    if (left >= 0 && left < width) {
      line[left] = color;
    }
    if (right >= 0 && right < width) {
      line[right] = color;
    }
  }
}
#endif

static bool lcd_flush_scaled_rect(lcd_dirty_rect_t dirty, uint16_t foreground, const uint16_t* background) {
  if (g_oled_panel == NULL || g_display_sleeping) {
    return true;
  }
  lcd_rect_t rect = dirty.rect;
  rect = lcd_rect_clipped(rect.x0, rect.y0, rect.x1, rect.y1);
  if (!lcd_rect_valid(rect)) {
    return true;
  }
  const int width = rect.x1 - rect.x0;
  const int height = rect.y1 - rect.y0;
  (void)height;
  const uint16_t* cached_bg = lcd_text_bg_cache_find(rect);
  const int max_rows = (int)(LCD_DIRTY_BUFFER_PIXELS / (size_t)width);
  if (max_rows <= 0) {
    return false;
  }
  for (int slice_y = rect.y0; slice_y < rect.y1; slice_y += max_rows) {
    const int slice_y1 =
        slice_y + max_rows < rect.y1 ? slice_y + max_rows : rect.y1;
    const lcd_rect_t slice = {
        .x0 = rect.x0,
        .y0 = slice_y,
        .x1 = rect.x1,
        .y1 = slice_y1,
    };
    for (int y = slice.y0; y < slice.y1; ++y) {
      lcd_render_scaled_rect_line(slice, rect, y, foreground, background, cached_bg);
    }
#if APP_LCD_DEBUG_DIRTY_RECTS
    if (dirty.debug_outline) {
      lcd_debug_outline_rect(rect, slice, dirty.text);
    }
#endif
    if (!oled_check(esp_lcd_panel_draw_bitmap(g_oled_panel,
                                              slice.x0,
                                              slice.y0,
                                              slice.x1,
                                              slice.y1,
                                              g_lcd_dirty_buffer),
                    "ST7789 draw dirty")) {
      g_oled_panel = NULL;
      return false;
    }
    if (xSemaphoreTake(g_lcd_transfer_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
      ESP_LOGW(TAG, "ST7789 dirty draw wait timed out");
      g_oled_panel = NULL;
      return false;
    }
  }
  return true;
}

static void lcd_flush_scaled_oled_frame_full(void) {
  if (g_oled_panel == NULL || g_display_sleeping) {
    return;
  }
  const bool light_theme = g_display.light_mode != 0;
  const uint16_t foreground = light_theme ? LCD_RGB565_BLACK : LCD_RGB565_WHITE;
  const uint16_t* background = light_theme ? g_lcd_background_light : g_lcd_background_dark;
  const lcd_dirty_rect_t full = {
      .rect = {.x0 = 0, .y0 = 0, .x1 = LCD_WIDTH, .y1 = LCD_HEIGHT},
      .text = false,
      .debug_outline = false,
  };
  if (!lcd_flush_scaled_rect(full, foreground, background)) {
    return;
  }
  lcd_clear_terminal_row_if_flipped();
  g_lcd_full_refresh_needed = false;
  g_lcd_last_flushed_page = g_display_page;
  g_lcd_last_flushed_light_mode = g_display.light_mode;
  g_lcd_last_flushed_flip_vertical = g_display.flip_vertical;
  g_lcd_last_flushed_text_outline = g_display.text_outline;
  lcd_dirty_remember_current_frame();
}

static void lcd_flush_scaled_oled_frame(void) {
  if (g_oled_panel == NULL || g_display_sleeping) {
    return;
  }
  lcd_dirty_finish_frame();
  if (g_lcd_last_flushed_page != g_display_page ||
      g_lcd_last_flushed_light_mode != g_display.light_mode ||
      g_lcd_last_flushed_flip_vertical != g_display.flip_vertical ||
      g_lcd_last_flushed_text_outline != g_display.text_outline) {
    g_lcd_full_refresh_needed = true;
    lcd_text_bg_cache_reset();
  }
  if (g_lcd_full_refresh_needed) {
    lcd_flush_scaled_oled_frame_full();
    return;
  }

  const bool light_theme = g_display.light_mode != 0;
  const uint16_t foreground = light_theme ? LCD_RGB565_BLACK : LCD_RGB565_WHITE;
  const uint16_t* background = light_theme ? g_lcd_background_light : g_lcd_background_dark;
  uint8_t dirty_count = 0;
  for (uint8_t i = 0; i < g_lcd_previous_rect_count; ++i) {
    lcd_rect_list_add(g_lcd_flush_rects,
                      &dirty_count,
                      g_lcd_previous_rects[i].rect,
                      false,
                      false);
  }
  for (uint8_t i = 0; i < g_lcd_current_rect_count; ++i) {
    lcd_rect_list_add(g_lcd_flush_rects,
                      &dirty_count,
                      g_lcd_current_rects[i].rect,
                      g_lcd_current_rects[i].text,
                      g_lcd_current_rects[i].debug_outline);
  }
  if (g_lcd_full_refresh_needed) {
    lcd_flush_scaled_oled_frame_full();
    return;
  }
  lcd_rect_list_merge(g_lcd_flush_rects, &dirty_count);

  for (uint8_t i = 0; i < dirty_count; ++i) {
    if (!lcd_flush_scaled_rect(g_lcd_flush_rects[i], foreground, background)) {
      if (g_oled_panel != NULL) {
        g_lcd_full_refresh_needed = true;
        lcd_flush_scaled_oled_frame_full();
      }
      return;
    }
  }
  lcd_clear_terminal_row_if_flipped();
  g_lcd_last_flushed_page = g_display_page;
  g_lcd_last_flushed_light_mode = g_display.light_mode;
  g_lcd_last_flushed_flip_vertical = g_display.flip_vertical;
  g_lcd_last_flushed_text_outline = g_display.text_outline;
  lcd_dirty_remember_current_frame();
}
#endif
