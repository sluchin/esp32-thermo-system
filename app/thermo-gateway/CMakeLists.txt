cmake_minimum_required(VERSION 3.20.0)

# デバッグログ (LOG_DBG と LOG_HEXDUMP_DBG) を有効にする. アプリのソースだけに, THERMO_LOG_LEVEL=4 (DBG) を付ける
# (Zephyr のモジュールは既定のレベルのまま. 全体を DBG にすると, ログの量が多すぎて動かなくなる).
# 例: west build -b xiao_esp32c3 app/thermo-gateway -- -DTHERMO_DEBUG_LOG=ON
option(THERMO_DEBUG_LOG "Enable the debug logs of the application" OFF)

# ZView (docs/zview.md) で, スレッドの CPU の使用率やヒープを見るための設定 (app/zview.conf) を足す.
# メモリと CPU を少し余分に使うので, 通常のビルドには入れない. native_sim では使わない.
# 例: west build -b xiao_esp32c3 app/thermo-gateway -- -DTHERMO_ZVIEW=ON
option(THERMO_ZVIEW "Add the Kconfig options for ZView" OFF)
if(THERMO_ZVIEW AND NOT BOARD MATCHES "^native_sim")
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_LIST_DIR}/../zview.conf)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(thermo-gateway)
# strnlen は POSIX の関数. native_sim (glibc, -std=c99) ではこの定義がないと宣言が見えない
target_compile_definitions(app PRIVATE _POSIX_C_SOURCE=200809L)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/ble.c)
target_sources(app PRIVATE src/switchbot.c)

# AWS IoT Core への送信 (WiFi + MQTT). ネットワークのないビルド (native_sim) では含めない
# (thermo-node と共有する, app/common/ のソース)
target_sources_ifdef(CONFIG_THERMO_CLOUD app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/cfg.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/cfg_shell.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/cloud.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/ntp.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/payload.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/wifi_link.c)

# thermo-node と thermo-gateway で共有する, BLE の UUID の定義 (app/common/)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
if(THERMO_DEBUG_LOG)
  target_compile_definitions(app PRIVATE THERMO_LOG_LEVEL=4)
endif()

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_SOURCE_DIR}/../warnings.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../version.cmake)
