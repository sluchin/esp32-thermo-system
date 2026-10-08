# thermo-node の node_time.c の単体テスト (Ztest).
# I2C のコントローラを, 偽のもの (vnd,test-i2c. app.overlay) に置き換える. 偽のコントローラは,
# PCF8563 のレジスタ (16 バイト) を持って, アドレスの設定と, 連続した読み書きに応える. 通信の失敗は,
# テストから設定する. アプリの Kconfig (CONFIG_THERMO_UTC_OFFSET_MIN) を使う.
cmake_minimum_required(VERSION 3.20.0)

# アプリの Kconfig を使う
set(KCONFIG_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../Kconfig)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_time_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_sources(app PRIVATE
  src/test_node_time.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/node_time.c)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
