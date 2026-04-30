#include "hex_utils.h"

#include "esp_attr.h"

static const DRAM_ATTR uint8_t HEX_VALUES[256] = {
    ['0'] = 1,
    ['1'] = 2,
    ['2'] = 3,
    ['3'] = 4,
    ['4'] = 5,
    ['5'] = 6,
    ['6'] = 7,
    ['7'] = 8,
    ['8'] = 9,
    ['9'] = 10,
    ['a'] = 11,
    ['b'] = 12,
    ['c'] = 13,
    ['d'] = 14,
    ['e'] = 15,
    ['f'] = 16,
    ['A'] = 11,
    ['B'] = 12,
    ['C'] = 13,
    ['D'] = 14,
    ['E'] = 15,
    ['F'] = 16,
};

static const DRAM_ATTR char HEX_DIGITS[] = "0123456789abcdef";

int app_hex_value(char ch) {
  return (int)HEX_VALUES[(uint8_t)ch] - 1;
}

char app_hex_digit(uint8_t value) {
  return HEX_DIGITS[value & 0x0f];
}
