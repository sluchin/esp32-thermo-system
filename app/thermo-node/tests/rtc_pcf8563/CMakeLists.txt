# thermo-node の drivers/rtc_pcf8563.c の単体テスト (Ztest).
# I2C のコントローラを, 偽のもの (vnd,test-i2c. app.overlay) に置き換える. 偽のコントローラは,
# PCF8563 のレジスタ (16 バイト) を持って, アドレスの設定と, 連続した読み書きに応える. 通信の失敗は,
# テストから設定する. binding (thermo,pcf8563) は, アプリの dts/bindings を使う.
cmake_minimum_required(VERSION 3.20.0)

# アプリの binding (thermo,pcf8563) を使う
list(APPEND DTS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_rtc_pcf8563_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_sources(app PRIVATE
  src/test_rtc_pcf8563.c
  ${CMAKE_CURRENT_LIST_DIR}/../../drivers/rtc_pcf8563.c)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
