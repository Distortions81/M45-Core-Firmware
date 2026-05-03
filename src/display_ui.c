#include "app_state.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_st7789.h"
#include "lcd_backgrounds.h"
#include "lcd_font.h"
#include "lcd_palette.h"
#else
#include "driver/i2c.h"
#include "esp_lcd_panel_ssd1306.h"
#endif
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "nvs.h"
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
#include "freertos/semphr.h"
#endif
#include "qrcode.h"

#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
#define LCD_SPI_HOST SPI2_HOST
#define LCD_WIDTH 320
#define LCD_HEIGHT 170
#define LCD_FRAME_SCALE 2
#define LCD_FRAME_WIDTH (OLED_WIDTH * LCD_FRAME_SCALE)
#define LCD_FRAME_HEIGHT (OLED_HEIGHT * LCD_FRAME_SCALE)
#define LCD_FRAME_X ((LCD_WIDTH - LCD_FRAME_WIDTH) / 2)
#define LCD_FRAME_Y ((LCD_HEIGHT - LCD_FRAME_HEIGHT) / 2)
#define LCD_MOSI_GPIO 23
#define LCD_SCLK_GPIO 18
#define LCD_CS_GPIO 15
#define LCD_DC_GPIO 2
#define LCD_RESET_GPIO 4
#define LCD_BACKLIGHT_GPIO 32
#ifndef APP_LCD_PIXEL_CLOCK_HZ
#define APP_LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#endif
#define LCD_PIXEL_CLOCK_HZ APP_LCD_PIXEL_CLOCK_HZ
#define LCD_BACKLIGHT_LEDC_MODE LEDC_LOW_SPEED_MODE
#define LCD_BACKLIGHT_LEDC_TIMER LEDC_TIMER_0
#define LCD_BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_0
#define LCD_BACKLIGHT_LEDC_RESOLUTION LEDC_TIMER_10_BIT
#define LCD_BACKLIGHT_LEDC_MAX_DUTY ((1U << 10) - 1U)
#define LCD_BACKLIGHT_LEDC_FREQUENCY 4000
#define LCD_GRAPH_HISTORY_LEN LCD_WIDTH
#define LCD_GRAPH_TOP 34
#define LCD_GRAPH_BOTTOM 137
#define LCD_GRAPH_HEIGHT (LCD_GRAPH_BOTTOM - LCD_GRAPH_TOP)
#define LCD_GRAPH_HEADER_Y0 0
#define LCD_GRAPH_HEADER_Y1 LCD_GRAPH_TOP
#define LCD_GRAPH_PLOT_Y0 LCD_GRAPH_TOP
#define LCD_GRAPH_PLOT_Y1 (LCD_GRAPH_BOTTOM + 1)
#define LCD_GRAPH_FOOTER_Y0 (LCD_GRAPH_BOTTOM + 1)
#define LCD_GRAPH_FOOTER_Y1 LCD_HEIGHT
#define LCD_GRAPH_MAGIC 0x4d344c47UL
#define LCD_GRAPH_VERSION 2
#define LCD_GRAPH_NVS_SAVE_INTERVAL_MINUTES 30
#define LCD_DIRTY_RECT_MAX 24
#define LCD_DIRTY_BUFFER_PIXELS (128 * 128)
#define LCD_TEXT_BG_CACHE_MAX 4
#define LCD_TEXT_BG_CACHE_PIXELS (4 * 1024)
#define LCD_DIRTY_MERGE_EXTRA_PIXELS 1024
#define LCD_DEBUG_DIRTY_RGB565 0xf800
#define LCD_DEBUG_TEXT_RGB565 0x07ff
#ifndef APP_LCD_DEBUG_DIRTY_RECTS
#define APP_LCD_DEBUG_DIRTY_RECTS 0
#endif
#define LCD_WORK_BUFFER_BYTES \
  ((LCD_DIRTY_BUFFER_PIXELS + LCD_TEXT_BG_CACHE_PIXELS) * sizeof(uint16_t))
#define LCD_MAX_WORK_BUFFER_BYTES (64 * 1024)
_Static_assert(LCD_WORK_BUFFER_BYTES <= LCD_MAX_WORK_BUFFER_BYTES,
               "LCD work buffers exceed the RAM budget");
#ifndef APP_LCD_X_GAP
#define APP_LCD_X_GAP 0
#endif
#ifndef APP_LCD_Y_GAP
#define APP_LCD_Y_GAP 35
#endif
#ifndef APP_LCD_INVERT_COLOR
#define APP_LCD_INVERT_COLOR 1
#endif
#define LCD_TEXT_PAGES ((LCD_HEIGHT + 7) / 8)
static uint16_t g_lcd_line[LCD_WIDTH];
static uint16_t g_lcd_dirty_buffer[LCD_DIRTY_BUFFER_PIXELS];
static uint16_t g_lcd_text_bg_cache_pixels[LCD_TEXT_BG_CACHE_PIXELS];
static uint8_t g_lcd_text_overlay[LCD_WIDTH * LCD_TEXT_PAGES];
static uint8_t g_lcd_text_outline[LCD_WIDTH * LCD_TEXT_PAGES];
static SemaphoreHandle_t g_lcd_transfer_done = NULL;
typedef struct {
  int x0;
  int y0;
  int x1;
  int y1;
} lcd_rect_t;
typedef struct {
  lcd_rect_t rect;
  bool text;
  bool debug_outline;
} lcd_dirty_rect_t;
typedef struct {
  lcd_rect_t rect;
  size_t pixel_offset;
} lcd_text_bg_cache_t;
static lcd_dirty_rect_t g_lcd_current_rects[LCD_DIRTY_RECT_MAX];
static lcd_dirty_rect_t g_lcd_previous_rects[LCD_DIRTY_RECT_MAX];
static lcd_dirty_rect_t g_lcd_flush_rects[LCD_DIRTY_RECT_MAX * 2];
static lcd_text_bg_cache_t g_lcd_text_bg_caches[LCD_TEXT_BG_CACHE_MAX];
static uint8_t g_lcd_current_rect_count = 0;
static uint8_t g_lcd_previous_rect_count = 0;
static uint8_t g_lcd_text_bg_cache_count = 0;
static size_t g_lcd_text_bg_cache_used = 0;
static lcd_rect_t g_lcd_pixel_rect = {0};
static bool g_lcd_pixel_rect_valid = false;
static bool g_lcd_full_refresh_needed = true;
static uint8_t g_lcd_last_flushed_page = 0xff;
static uint8_t g_lcd_last_flushed_light_mode = 0xff;
static uint8_t g_lcd_last_flushed_flip_vertical = 0xff;
static uint8_t g_lcd_last_flushed_text_outline = 0xff;
static bool g_lcd_graph_force_full = true;
static uint32_t g_lcd_graph_last_hashrate = UINT32_MAX;
static uint32_t g_lcd_graph_last_best_share = UINT32_MAX;
static uint32_t g_lcd_graph_last_scale_hashrate = UINT32_MAX;
static uint32_t g_lcd_graph_last_scale_share = UINT32_MAX;
static uint8_t g_lcd_graph_last_current_level = 0xff;
static char g_lcd_graph_last_ip[24];
static char g_lcd_graph_last_hashrate_text[20];
static char g_lcd_graph_last_pool[80];
static char g_lcd_graph_last_uptime[20];
typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint16_t head;
  uint16_t reserved;
  uint32_t scale_hashrate;
  uint32_t scale_share;
  uint64_t last_bucket_hashes;
  uint32_t last_bucket_shares;
  int64_t last_bucket_minute;
  uint8_t levels[LCD_GRAPH_HISTORY_LEN];
  uint8_t share_levels[LCD_GRAPH_HISTORY_LEN];
} lcd_graph_history_t;
static RTC_NOINIT_ATTR lcd_graph_history_t g_lcd_graph_history;
static bool g_lcd_graph_nvs_loaded = false;
static bool g_lcd_graph_dirty = false;
static int64_t g_lcd_graph_last_saved_minute = -1;
#else
#define OLED_I2C_PORT I2C_NUM_0
#ifndef APP_OLED_I2C_CLOCK_HZ
#define APP_OLED_I2C_CLOCK_HZ (400 * 1000)
#endif
#endif
#define QR_MAX_VERSION 6
#define QR_RUNTIME_VERSION 3
#define QR_SETUP_VERSION 3
#define QR_MAX_MODULES ((4 * QR_MAX_VERSION) + 17)
#define QR_MAX_BUFFER_SIZE (((QR_MAX_MODULES * QR_MAX_MODULES) + 7) / 8)
#define QR_SCALE 2
#define QR_TILE_SIZE 64
#define QR_TILE_PAGES (QR_TILE_SIZE / 8)
#define QR_TEXT_GAP 4
#define RUNTIME_TEXT_WIDTH (OLED_WIDTH - QR_TILE_SIZE - QR_TEXT_GAP)
#define TEXT_CACHE_MAX_WIDTH OLED_WIDTH
#define TEXT_CACHE_MAX_PAGES 2
#define TEXT_CACHE_MAX_CHARS 64

static const char* TAG = "oled";
static esp_lcd_panel_handle_t g_oled_panel = NULL;
static esp_lcd_panel_io_handle_t g_oled_io_handle = NULL;
static bool g_display_sleeping = false;
static uint8_t g_oled_frame[OLED_WIDTH * OLED_HEIGHT / 8];
#if !APP_DISPLAY_IDEASPARK_ESP32_19_LCD
#define OLED_PAGE_COUNT (OLED_HEIGHT / 8)
static uint8_t g_oled_previous_frame[OLED_WIDTH * OLED_PAGE_COUNT];
static bool g_oled_full_refresh_needed = true;
#endif
static bool g_runtime_qr_on_right = false;
static bool g_runtime_inverted = false;
static bool g_runtime_swap_sides_on_next_block = false;
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static bool g_lcd_clear_runtime_text_slots = false;
#endif
static uint8_t g_applied_flip_vertical = 0xff;
static uint8_t g_applied_brightness_pct = 0xff;
static uint8_t g_display_page = 0;
static char g_last_runtime_block[sizeof(g_stratum_runtime.current_block)];
#if !APP_DISPLAY_IDEASPARK_ESP32_19_LCD
#define OLED_GRAPH_HISTORY_LEN OLED_WIDTH
#define OLED_GRAPH_TOP 13
#define OLED_GRAPH_BOTTOM 52
#define OLED_GRAPH_HEIGHT (OLED_GRAPH_BOTTOM - OLED_GRAPH_TOP)
static uint8_t g_oled_graph_levels[OLED_GRAPH_HISTORY_LEN];
static uint8_t g_oled_graph_share_levels[OLED_GRAPH_HISTORY_LEN];
static uint8_t g_oled_graph_count = 0;
static uint8_t g_oled_graph_head = 0;
static uint32_t g_oled_graph_scale_hashrate = 1000;
static uint32_t g_oled_graph_scale_share = 2;
static uint64_t g_oled_graph_last_bucket_hashes = 0;
static uint32_t g_oled_graph_last_bucket_shares = 0;
static int64_t g_oled_graph_last_bucket_minute = -1;
#endif

static void oled_draw_text_clipped(int x, int y, const char* text, int scale, int max_width);
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static void oled_draw_text_glyph_bounds(int x, int y, const char* text, int scale, int max_width);
#endif
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
static void lcd_clear_terminal_row_if_flipped(void);
static void lcd_graph_load_nvs(void);
#endif

typedef struct {
  bool valid;
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  bool lcd_dirty;
  bool lcd_drawn;
  int lcd_x;
  int lcd_y;
#endif
  QRCode qr;
  uint8_t data[QR_MAX_BUFFER_SIZE];
  uint8_t tile[QR_TILE_SIZE * QR_TILE_PAGES];
} oled_qr_cache_t;

static oled_qr_cache_t g_setup_qr_cache = {0};
static oled_qr_cache_t g_runtime_qr_cache = {0};

typedef struct {
  bool valid;
  uint8_t width;
  uint8_t y_offset;
  uint8_t pages;
  char text[TEXT_CACHE_MAX_CHARS];
  uint8_t pixels[TEXT_CACHE_MAX_WIDTH * TEXT_CACHE_MAX_PAGES];
} oled_text_cache_t;

static oled_text_cache_t g_runtime_ip_cache = {0};
static oled_text_cache_t g_pool_endpoint_cache = {0};

static void oled_prepare_runtime_ip_cache(void);

static bool oled_check(esp_err_t result, const char* operation) {
  if (result == ESP_OK) {
    return true;
  }
  APP_SERIAL_LOGF("oled: %s failed: %s\n", operation, esp_err_to_name(result));
  ESP_LOGW(TAG, "%s failed: %s", operation, esp_err_to_name(result));
  return false;
}

/* Private implementation fragments share the static state above. */
#include "display_ui_lcd_core.inc"
#include "display_ui_panel.inc"
#include "display_ui_render.inc"
#include "display_ui_pages.inc"
#include "display_ui_lcd_graph.inc"
#include "display_ui_lcd_flush.inc"
#include "display_ui_ssd1306_flush.inc"
#include "display_ui_update.inc"
