cmake_minimum_required(VERSION 3.20.0)

# デバッグログ (LOG_DBG と LOG_HEXDUMP_DBG) を有効にする. アプリのソースだけに, THERMO_LOG_LEVEL=4 (DBG) を付ける
# (Zephyr のモジュールは, 既定のレベルのまま. 全体を DBG にすると, ログの量が多すぎて, 動かなくなる).
# 例: west build -b xiao_esp32c3 app/thermo-node -- -DTHERMO_DEBUG_LOG=ON
option(THERMO_DEBUG_LOG "Enable the debug logs of the application" OFF)

# デバッグログのときは, センサのドライバの DBG ログも出す. DHT11 のドライバは, 「データのチェックサムが
# 合わない」(センサは応答したが, BLE の割り込みなどで読み取りが乱れた) を, DBG ログに出す.
# このログがなく, -EIO だけのときは, センサの応答がない (配線, 接触, 電源). native_sim には, センサの
# ドライバがないので, 付けない
if(THERMO_DEBUG_LOG AND NOT BOARD MATCHES "^native_sim")
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_LIST_DIR}/debug.conf)
endif()

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
# 起動画面と起動音 (CONFIG_THERMO_BOOT_SCREEN と CONFIG_THERMO_BUZZER が, どちらも n のときは, ビルドしない).
# 素材 (絵, 題名, メロディ) は, private/src/boot_assets.c (別のプライベートリポジトリを, サブモジュールとして置く)
# があれば, それを使う. なければ, このアプリのオリジナルの素材 (src/boot_assets.c). SETUP.md を参照
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/private/src/boot_assets.c)
  set(THERMO_BOOT_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/private/src/boot_assets.c)
else()
  set(THERMO_BOOT_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/src/boot_assets.c)
endif()
if(CONFIG_THERMO_BOOT_SCREEN OR CONFIG_THERMO_BUZZER)
  target_sources(app PRIVATE ${THERMO_BOOT_ASSETS})
  # private/ の素材が, src/ のヘッダ (boot_assets.h, buzzer.h) を, #include できるようにする
  target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()
target_sources_ifdef(CONFIG_THERMO_BUZZER app PRIVATE src/buzzer.c)

# RTC の時刻と, PCF8563 のドライバ (CONFIG_THERMO_RTC=n のときは, ビルドしない). binding は,
# dts/bindings/rtc/thermo,pcf8563.yaml (アプリのディレクトリは, Devicetree のルートになる)
target_sources_ifdef(CONFIG_THERMO_RTC app PRIVATE src/node_time.c drivers/rtc_pcf8563.c)

# WiFi と SNTP による時刻の取得 (CONFIG_THERMO_WIFI_TIME=n のときは, ビルドしない)
target_sources_ifdef(CONFIG_THERMO_WIFI_TIME app PRIVATE
  src/wifi_cfg.c
  src/wifi_link.c
  src/wifi_shell.c
  src/wifi_time.c)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_SOURCE_DIR}/../warnings.cmake)
