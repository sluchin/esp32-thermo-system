cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(thermo-gateway)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/ble.c)
target_sources(app PRIVATE src/switchbot.c)

# AWS IoT Core への送信 (WiFi + MQTT)。ネットワークのないビルド (native_sim) では、含めない
target_sources_ifdef(CONFIG_THERMO_CLOUD app PRIVATE
  src/cfg.c
  src/cfg_shell.c
  src/cloud.c
  src/ntp.c
  src/payload.c
  src/wifi_link.c)

# thermo-node と thermo-gateway で共有する, BLE の UUID の定義 (app/common/)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_SOURCE_DIR}/../warnings.cmake)
