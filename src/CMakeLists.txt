option(APP_DISPLAY_IDEASPARK_ESP32_19_LCD "Build for ideaspark ESP32 1.9 inch ST7789 LCD board" OFF)
option(APP_LCD_DEBUG_DIRTY_RECTS "Draw red/cyan outlines around LCD dirty rectangles" OFF)

set(WEB_ASSETS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/web_assets.h")
set(BUILD_INFO_HEADER "${CMAKE_CURRENT_BINARY_DIR}/build_info.h")
set(LCD_BACKGROUNDS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/lcd_backgrounds.h")
set(LCD_FONT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/lcd_font.h")
set(LCD_PALETTE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/lcd_palette.h")
set(APP_SCRIPT_TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}/../scripts/tools")
if(NOT CMAKE_SCRIPT_MODE_FILE)
    set(WEB_ASSET_FILES
        "${CMAKE_CURRENT_LIST_DIR}/../web/footer.html"
        "${CMAKE_CURRENT_LIST_DIR}/../web/licenses_page.html"
        "${CMAKE_CURRENT_LIST_DIR}/../web/settings_page.html"
        "${CMAKE_CURRENT_LIST_DIR}/../web/setup_page.html"
        "${CMAKE_CURRENT_LIST_DIR}/../web/stats_page.html"
        "${CMAKE_CURRENT_LIST_DIR}/../web/styles.css"
        "${CMAKE_CURRENT_LIST_DIR}/../web/themes.html"
        "${CMAKE_CURRENT_LIST_DIR}/../web/toolbar.html"
        "${CMAKE_CURRENT_LIST_DIR}/../LICENSES/project/MIT.txt"
        "${CMAKE_CURRENT_LIST_DIR}/../LICENSES/third-party/Apache-2.0.txt"
        "${CMAKE_CURRENT_LIST_DIR}/../LICENSES/third-party/QRCode.txt"
    )

    add_custom_command(
        OUTPUT "${WEB_ASSETS_HEADER}"
        COMMAND "${PYTHON}" "${APP_SCRIPT_TOOLS_DIR}/embed_web_assets.py"
                "${WEB_ASSETS_HEADER}" "${CMAKE_CURRENT_LIST_DIR}/../web" ${WEB_ASSET_FILES}
        DEPENDS "${APP_SCRIPT_TOOLS_DIR}/embed_web_assets.py" ${WEB_ASSET_FILES}
        VERBATIM
    )
    add_custom_target(web_assets DEPENDS "${WEB_ASSETS_HEADER}")

    add_custom_command(
        OUTPUT "${BUILD_INFO_HEADER}"
        COMMAND "${PYTHON}" "${APP_SCRIPT_TOOLS_DIR}/generate_build_info.py"
                "${BUILD_INFO_HEADER}" "${CMAKE_CURRENT_LIST_DIR}/.."
        DEPENDS "${APP_SCRIPT_TOOLS_DIR}/generate_build_info.py"
        VERBATIM
    )
    add_custom_target(build_info DEPENDS "${BUILD_INFO_HEADER}")

    if(APP_DISPLAY_IDEASPARK_ESP32_19_LCD)
        set(APP_PNG_PYTHON "/usr/bin/python3")
        file(GLOB LCD_BACKGROUND_FILES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_LIST_DIR}/../bgl.png"
            "${CMAKE_CURRENT_LIST_DIR}/../bgd.png"
        )
        add_custom_command(
            OUTPUT "${LCD_BACKGROUNDS_HEADER}"
            COMMAND "${APP_PNG_PYTHON}" "${APP_SCRIPT_TOOLS_DIR}/convert_lcd_backgrounds.py"
                    "${LCD_BACKGROUNDS_HEADER}"
                    "${CMAKE_CURRENT_LIST_DIR}/../bgl.png"
                    "${CMAKE_CURRENT_LIST_DIR}/../bgd.png"
            DEPENDS "${APP_SCRIPT_TOOLS_DIR}/convert_lcd_backgrounds.py" ${LCD_BACKGROUND_FILES}
            VERBATIM
        )
        add_custom_target(lcd_background_assets DEPENDS "${LCD_BACKGROUNDS_HEADER}")

        add_custom_command(
            OUTPUT "${LCD_FONT_HEADER}"
            COMMAND "${APP_PNG_PYTHON}" "${APP_SCRIPT_TOOLS_DIR}/generate_lcd_font.py"
                    "${LCD_FONT_HEADER}"
            DEPENDS "${APP_SCRIPT_TOOLS_DIR}/generate_lcd_font.py"
            VERBATIM
        )
        add_custom_target(lcd_font_asset DEPENDS "${LCD_FONT_HEADER}")

        add_custom_command(
            OUTPUT "${LCD_PALETTE_HEADER}"
            COMMAND "${APP_PNG_PYTHON}" "${APP_SCRIPT_TOOLS_DIR}/generate_lcd_palette.py"
                    "${LCD_PALETTE_HEADER}"
            DEPENDS "${APP_SCRIPT_TOOLS_DIR}/generate_lcd_palette.py"
            VERBATIM
        )
        add_custom_target(lcd_palette_asset DEPENDS "${LCD_PALETTE_HEADER}")
    endif()
endif()

idf_component_register(
    SRCS
        "idf_main.c"
        "hex_utils.c"
        "miner_candidate_loop_xtensa.S"
        "network.c"
        "display_ui.c"
        "status_text.c"
        "stratum_miner.c"
        "third_party/qrcode/qrcode.c"
        "web_server.c"
    INCLUDE_DIRS "." "third_party/qrcode" "${CMAKE_CURRENT_BINARY_DIR}"
    PRIV_REQUIRES
        driver
        esp_event
        esp_http_server
        esp_lcd
        esp_netif
        esp_pm
        esp_timer
        esp_wifi
        freertos
        lwip
        mbedtls
        nvs_flash
)

option(APP_SYNTHETIC_NO_WIFI_NO_OLED "Build no-Wi-Fi/no-OLED synthetic miner benchmark firmware" OFF)
if(APP_SYNTHETIC_NO_WIFI_NO_OLED)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_SYNTHETIC_NO_WIFI_NO_OLED=1 APP_ENABLE_SERIAL_LOGS=1)
endif()

option(APP_ENABLE_SERIAL_LOGS "Enable firmware serial printf diagnostics" OFF)
if(APP_ENABLE_SERIAL_LOGS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_ENABLE_SERIAL_LOGS=1)
endif()

option(APP_TEST_BLOCK_FOUND "Seed a synthetic block candidate alert at boot" OFF)
if(APP_TEST_BLOCK_FOUND)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_TEST_BLOCK_FOUND=1)
endif()

if(APP_DISPLAY_IDEASPARK_ESP32_19_LCD)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_DISPLAY_IDEASPARK_ESP32_19_LCD=1)
endif()

if(APP_LCD_DEBUG_DIRTY_RECTS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_LCD_DEBUG_DIRTY_RECTS=1)
endif()

set(APP_LCD_PIXEL_CLOCK_HZ "" CACHE STRING "ST7789 LCD SPI pixel clock in Hz")
if(NOT "${APP_LCD_PIXEL_CLOCK_HZ}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_LCD_PIXEL_CLOCK_HZ=${APP_LCD_PIXEL_CLOCK_HZ})
endif()

set(APP_OLED_I2C_CLOCK_HZ "" CACHE STRING "SSD1306 OLED I2C clock in Hz")
if(NOT "${APP_OLED_I2C_CLOCK_HZ}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_OLED_I2C_CLOCK_HZ=${APP_OLED_I2C_CLOCK_HZ})
endif()

set(APP_STRATUM_MINE_BATCH "" CACHE STRING "Runtime Stratum miner nonce batch size")
if(NOT "${APP_STRATUM_MINE_BATCH}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_STRATUM_MINE_BATCH=${APP_STRATUM_MINE_BATCH})
endif()

set(APP_STRATUM_MINE_YIELD_BATCHES "" CACHE STRING "Runtime Stratum miner batches between task yields")
if(NOT "${APP_STRATUM_MINE_YIELD_BATCHES}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_STRATUM_MINE_YIELD_BATCHES=${APP_STRATUM_MINE_YIELD_BATCHES})
endif()

set(APP_SYNTHETIC_HASHES_PER_TASK "" CACHE STRING "Synthetic benchmark hashes per miner task")
if(NOT "${APP_SYNTHETIC_HASHES_PER_TASK}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_SYNTHETIC_HASHES_PER_TASK=${APP_SYNTHETIC_HASHES_PER_TASK})
endif()

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_dependencies(${COMPONENT_LIB} web_assets build_info)
    if(APP_DISPLAY_IDEASPARK_ESP32_19_LCD)
        add_dependencies(${COMPONENT_LIB} lcd_background_assets lcd_font_asset lcd_palette_asset)
    endif()
endif()
