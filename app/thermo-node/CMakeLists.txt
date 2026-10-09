cmake_minimum_required(VERSION 3.20.0)

# デバッグログ (LOG_DBG と LOG_HEXDUMP_DBG) を有効にする. アプリのソースだけに, THERMO_LOG_LEVEL=4 (DBG) を付ける
# (Zephyr のモジュールは, 既定のレベルのまま. 全体を DBG にすると, ログの量が多すぎて, 動かなくなる).
# 例: west build -b xiao_esp32c3 app/thermo-node -- -DTHERMO_DEBUG_LOG=ON
option(THERMO_DEBUG_LOG "Enable the debug logs of the application" OFF)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(thermo-node)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/ble.c)

# thermo-node と thermo-gateway で共有する, BLE の UUID の定義 (app/common/)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
if(THERMO_DEBUG_LOG)
  target_compile_definitions(app PRIVATE THERMO_LOG_LEVEL=4)
endif()
target_sources(app PRIVATE src/sensor.c)
# OLED (CONFIG_THERMO_DISPLAY=n のときは, ビルドしない)
target_sources_ifdef(CONFIG_THERMO_DISPLAY app PRIVATE src/oled.c src/history.c src/button.c)
# RTC の時刻と, PCF8563 のドライバ (CONFIG_THERMO_RTC=n のときは, ビルドしない). binding は,
# dts/bindings/rtc/thermo,pcf8563.yaml (アプリのディレクトリは, Devicetree のルートになる)
target_sources_ifdef(CONFIG_THERMO_RTC app PRIVATE src/node_time.c drivers/rtc_pcf8563.c)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_SOURCE_DIR}/../warnings.cmake)
