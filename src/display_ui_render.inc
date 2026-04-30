/* Private implementation fragment included by display_ui.c. */
#define FONT_WIDTH 4
#define FONT_HEIGHT 6
#define FONT_ADVANCE 5
#define FONT_FIRST_CHAR 32
#define FONT_CHAR_COUNT 96

static const uint8_t* glyph4x6_rows(char ch) {
  static const uint8_t blank[FONT_HEIGHT] = {0};
  static const uint8_t digits[10][FONT_HEIGHT] = {
      {6, 9, 9, 9, 9, 6},  {2, 6, 2, 2, 2, 7},  {6, 9, 1, 2, 4, 15}, {14, 1, 6, 1, 9, 6},
      {2, 6, 10, 15, 2, 2}, {15, 8, 14, 1, 9, 6}, {6, 8, 14, 9, 9, 6}, {15, 1, 2, 4, 4, 4},
      {6, 9, 6, 9, 9, 6},  {6, 9, 9, 7, 1, 6},
  };
  static const uint8_t letters[26][FONT_HEIGHT] = {
      {6, 9, 9, 15, 9, 9},  {14, 9, 14, 9, 9, 14}, {7, 8, 8, 8, 8, 7},   {14, 9, 9, 9, 9, 14},
      {15, 8, 14, 8, 8, 15}, {15, 8, 14, 8, 8, 8},  {7, 8, 8, 11, 9, 7},  {9, 9, 15, 9, 9, 9},
      {7, 2, 2, 2, 2, 7},    {1, 1, 1, 1, 9, 6},    {9, 10, 12, 10, 9, 9}, {8, 8, 8, 8, 8, 15},
      {9, 15, 15, 9, 9, 9},  {9, 13, 15, 11, 9, 9}, {6, 9, 9, 9, 9, 6},   {14, 9, 9, 14, 8, 8},
      {6, 9, 9, 9, 11, 7},   {14, 9, 9, 14, 10, 9}, {7, 8, 6, 1, 1, 14},  {15, 2, 2, 2, 2, 2},
      {9, 9, 9, 9, 9, 6},    {9, 9, 9, 9, 6, 6},    {9, 9, 9, 15, 15, 9}, {9, 9, 6, 6, 9, 9},
      {9, 9, 6, 2, 2, 2},    {15, 1, 2, 4, 8, 15},
  };
  static const uint8_t glyph_dash[FONT_HEIGHT] = {0, 0, 0, 15, 0, 0};
  static const uint8_t glyph_dot[FONT_HEIGHT] = {0, 0, 0, 0, 0, 6};
  static const uint8_t glyph_colon[FONT_HEIGHT] = {0, 6, 0, 0, 6, 0};
  static const uint8_t glyph_slash[FONT_HEIGHT] = {1, 1, 2, 2, 4, 4};
  static const uint8_t glyph_underscore[FONT_HEIGHT] = {0, 0, 0, 0, 0, 15};
  static const uint8_t glyph_plus[FONT_HEIGHT] = {0, 0, 2, 15, 2, 0};
  static const uint8_t glyph_percent[FONT_HEIGHT] = {9, 1, 2, 8, 9, 2};

  if (ch >= '0' && ch <= '9') {
    return digits[ch - '0'];
  }
  if (ch >= 'a' && ch <= 'z') {
    ch = (char)(ch - 'a' + 'A');
  }
  if (ch >= 'A' && ch <= 'Z') {
    return letters[ch - 'A'];
  }
  switch (ch) {
    case '-':
      return glyph_dash;
    case '.':
      return glyph_dot;
    case ':':
      return glyph_colon;
    case '/':
      return glyph_slash;
    case '_':
      return glyph_underscore;
    case '+':
      return glyph_plus;
    case '%':
      return glyph_percent;
    default:
      return blank;
  }
}

static uint8_t glyph4x6(char ch, uint8_t row) {
  return glyph4x6_rows(ch)[row];
}

static const uint8_t* glyph4x6_columns(char ch) {
  static bool initialized = false;
  static uint8_t columns[FONT_CHAR_COUNT][FONT_WIDTH];
  static const uint8_t blank[FONT_WIDTH] = {0};

  if (!initialized) {
    for (int glyph = 0; glyph < FONT_CHAR_COUNT; ++glyph) {
      const uint8_t* rows = glyph4x6_rows((char)(glyph + FONT_FIRST_CHAR));
      for (int col = 0; col < FONT_WIDTH; ++col) {
        uint8_t column = 0;
        const uint8_t mask = (uint8_t)(1U << (FONT_WIDTH - 1 - col));
        for (int row = 0; row < FONT_HEIGHT; ++row) {
          if ((rows[row] & mask) != 0) {
            column |= (uint8_t)(1U << row);
          }
        }
        columns[glyph][col] = column;
      }
    }
    initialized = true;
  }

  const unsigned char index = (unsigned char)ch;
  if (index < FONT_FIRST_CHAR || index >= FONT_FIRST_CHAR + FONT_CHAR_COUNT) {
    return blank;
  }
  return columns[index - FONT_FIRST_CHAR];
}

static void render_glyph_1x_columns(uint8_t* pixels, int stride, int page_count, int x, int y, char ch) {
  if (x < 0 || x + FONT_WIDTH > stride || y < 0 || y >= page_count * 8) {
    return;
  }

  const uint8_t* columns = glyph4x6_columns(ch);
  const int page = y >> 3;
  const int y_shift = y & 7;
  for (int col = 0; col < FONT_WIDTH; ++col) {
    const uint8_t column = columns[col];
    if (column == 0) {
      continue;
    }
    pixels[x + col + page * stride] |= (uint8_t)(column << y_shift);
    if (y_shift > 8 - FONT_HEIGHT && page + 1 < page_count) {
      pixels[x + col + (page + 1) * stride] |= (uint8_t)(column >> (8 - y_shift));
    }
  }
}

static void oled_set_pixel(int x, int y, bool on) {
  if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
    return;
  }
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  if (on) {
    lcd_mark_scaled_oled_pixel(x, y);
  }
#endif
  uint8_t* byte = &g_oled_frame[x + (y / 8) * OLED_WIDTH];
  const uint8_t mask = (uint8_t)(1U << (y & 7));
  if (on) {
    *byte |= mask;
  } else {
    *byte &= (uint8_t)~mask;
  }
}

#if !APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static void oled_invert_pixel(int x, int y) {
  if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
    return;
  }
  g_oled_frame[x + (y / 8) * OLED_WIDTH] ^= (uint8_t)(1U << (y & 7));
}
#endif

static void text_cache_render(oled_text_cache_t* cache, const char* text, int width, int y_offset) {
  cache->valid = true;
  cache->width = (uint8_t)(width > TEXT_CACHE_MAX_WIDTH ? TEXT_CACHE_MAX_WIDTH : width);
  cache->y_offset = (uint8_t)y_offset;
  cache->pages = (uint8_t)((y_offset + FONT_HEIGHT + 7) / 8);
  if (cache->pages > TEXT_CACHE_MAX_PAGES) {
    cache->pages = TEXT_CACHE_MAX_PAGES;
  }
  copy_str(cache->text, sizeof(cache->text), text);
  memset(cache->pixels, 0, cache->width * cache->pages);

  int x = 0;
  const char* cursor = text;
  while (*cursor != '\0' && x <= cache->width - FONT_WIDTH && x + FONT_WIDTH <= cache->width) {
    render_glyph_1x_columns(cache->pixels, cache->width, cache->pages, x, y_offset, *cursor++);
    x += FONT_ADVANCE;
  }
}

#if !APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static void oled_draw_text_cached(oled_text_cache_t* cache, int x, int y, const char* text, int max_width) {
  if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT || max_width <= 0) {
    return;
  }
  const int width = max_width > OLED_WIDTH - x ? OLED_WIDTH - x : max_width;
  const int y_offset = y & 7;
  const int start_page = y / 8;
  if (!cache->valid || cache->width != width || cache->y_offset != y_offset || strcmp(cache->text, text) != 0) {
    text_cache_render(cache, text, width, y_offset);
  }
  for (int page = 0; page < cache->pages && start_page + page < OLED_HEIGHT / 8; ++page) {
    memcpy(&g_oled_frame[x + (start_page + page) * OLED_WIDTH], &cache->pixels[page * cache->width], cache->width);
  }
}
#endif

static void qr_tile_set_pixel(oled_qr_cache_t* cache, int x, int y) {
  if (x < 0 || x >= QR_TILE_SIZE || y < 0 || y >= QR_TILE_SIZE) {
    return;
  }
  cache->tile[x + (y / 8) * QR_TILE_SIZE] |= (uint8_t)(1U << (y & 7));
}

static void oled_draw_text(int x, int y, const char* text, int scale) {
  if (scale == 1) {
    oled_draw_text_clipped(x, y, text, scale, OLED_WIDTH - x);
    return;
  }
  while (*text != '\0' && x <= OLED_WIDTH - FONT_WIDTH * scale) {
    const char ch = *text++;
    for (int row = 0; row < FONT_HEIGHT; ++row) {
      const uint8_t bits = glyph4x6(ch, row);
      for (int col = 0; col < FONT_WIDTH; ++col) {
        if ((bits & (1U << (FONT_WIDTH - 1 - col))) == 0) {
          continue;
        }
        for (int dy = 0; dy < scale; ++dy) {
          for (int dx = 0; dx < scale; ++dx) {
            oled_set_pixel(x + col * scale + dx, y + row * scale + dy, true);
          }
        }
      }
    }
    x += FONT_ADVANCE * scale;
  }
}

static void oled_draw_text_clipped(int x, int y, const char* text, int scale, int max_width) {
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  if (scale == 1) {
    lcd_draw_text_clipped(x, y, text, max_width);
    return;
  }
#endif
  if (scale == 1) {
    const int end_x = x + max_width;
    while (*text != '\0' && x >= 0 && x <= OLED_WIDTH - FONT_WIDTH && x + FONT_WIDTH <= end_x) {
      render_glyph_1x_columns(g_oled_frame, OLED_WIDTH, OLED_HEIGHT / 8, x, y, *text++);
      x += FONT_ADVANCE;
    }
    return;
  }
  const int start_x = x;
  while (*text != '\0' && x <= OLED_WIDTH - FONT_WIDTH * scale && x + FONT_WIDTH * scale <= start_x + max_width) {
    const char ch = *text++;
    for (int row = 0; row < FONT_HEIGHT; ++row) {
      const uint8_t bits = glyph4x6(ch, row);
      for (int col = 0; col < FONT_WIDTH; ++col) {
        if ((bits & (1U << (FONT_WIDTH - 1 - col))) == 0) {
          continue;
        }
        for (int dy = 0; dy < scale; ++dy) {
          for (int dx = 0; dx < scale; ++dx) {
            oled_set_pixel(x + col * scale + dx, y + row * scale + dy, true);
          }
        }
      }
    }
    x += FONT_ADVANCE * scale;
  }
}

#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static void oled_draw_text_glyph_bounds(int x, int y, const char* text, int scale, int max_width) {
  if (scale == 1) {
    lcd_draw_text_glyph_bounds(x, y, text, max_width);
    return;
  }
  oled_draw_text_clipped(x, y, text, scale, max_width);
}
#endif

static bool oled_generate_qr(oled_qr_cache_t* cache, const char* payload, uint8_t version) {
  if (version > QR_MAX_VERSION) {
    cache->valid = false;
    return false;
  }
  if (qrcode_initText(&cache->qr, cache->data, version, ECC_LOW, payload) != 0) {
    cache->valid = false;
    return false;
  }
  const int qr_pixels = cache->qr.size * QR_SCALE;
  if (qr_pixels > QR_TILE_SIZE) {
    cache->valid = false;
    return false;
  }
  memset(cache->tile, 0, sizeof(cache->tile));
  const int offset = (QR_TILE_SIZE - qr_pixels) / 2;
  for (int row = 0; row < cache->qr.size; ++row) {
    for (int col = 0; col < cache->qr.size; ++col) {
      if (!qrcode_getModule(&cache->qr, (uint8_t)col, (uint8_t)row)) {
        continue;
      }
      const int px = offset + col * QR_SCALE;
      const int py = offset + row * QR_SCALE;
      for (int dy = 0; dy < QR_SCALE; ++dy) {
        for (int dx = 0; dx < QR_SCALE; ++dx) {
          qr_tile_set_pixel(cache, px + dx, py + dy);
        }
      }
    }
  }
  cache->valid = true;
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  cache->lcd_dirty = true;
#endif
  return true;
}

static bool oled_draw_cached_qr(oled_qr_cache_t* cache, int x, int y) {
  if (!cache->valid) {
    oled_draw_text(x, y, "QR ERROR", 1);
    return false;
  }
  if (x < 0 || x + QR_TILE_SIZE > OLED_WIDTH || y != 0) {
    return false;
  }
  for (int page = 0; page < QR_TILE_PAGES; ++page) {
    memcpy(&g_oled_frame[x + page * OLED_WIDTH], &cache->tile[page * QR_TILE_SIZE], QR_TILE_SIZE);
  }
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  if (cache->lcd_dirty || !cache->lcd_drawn || cache->lcd_x != x || cache->lcd_y != y) {
    if (cache->lcd_drawn && (cache->lcd_x != x || cache->lcd_y != y)) {
      lcd_mark_scaled_oled_rect(cache->lcd_x,
                                cache->lcd_y,
                                cache->lcd_x + QR_TILE_SIZE,
                                cache->lcd_y + QR_TILE_SIZE);
    }
    lcd_mark_scaled_oled_rect(x, y, x + QR_TILE_SIZE, y + QR_TILE_SIZE);
    cache->lcd_x = x;
    cache->lcd_y = y;
    cache->lcd_drawn = true;
    cache->lcd_dirty = false;
  }
#endif
  return true;
}

void oled_prepare_runtime_qr(void) {
  char payload[48];
  snprintf(payload, sizeof(payload), "http://%s/", g_sta_ip[0] != '\0' ? g_sta_ip : g_ap_ip_text);
  oled_generate_qr(&g_runtime_qr_cache, payload, QR_RUNTIME_VERSION);
  oled_prepare_runtime_ip_cache();
}
