cmake_minimum_required(VERSION 3.20.0)

# デバッグログ (LOG_DBG と LOG_HEXDUMP_DBG) を有効にする (CONFIG_LOG_DEFAULT_LEVEL=4).
# 例: west build -b xiao_esp32c3 app/thermo-node -- -DTHERMO_DEBUG_LOG=ON
option(THERMO_DEBUG_LOG "Enable the debug logs (CONFIG_LOG_DEFAULT_LEVEL=4)" OFF)
if(THERMO_DEBUG_LOG)
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../debug-log.conf)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(thermo-node)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/ble.c)

# thermo-node と thermo-gateway で共有する, BLE の UUID の定義 (app/common/)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_sources(app PRIVATE src/sensor.c)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_SOURCE_DIR}/../warnings.cmake)
