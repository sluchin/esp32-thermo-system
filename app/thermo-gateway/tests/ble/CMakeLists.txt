# thermo-gateway の ble.c の単体テスト (Ztest + FFF).
# Bluetooth スタックは使わず, bt_enable() などを, FFF のモックに置き換える.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_gateway_ble_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
# CONFIG_BT_MAX_CONN は, Bluetooth を有効にしたときだけ, Kconfig が定義する (ここでは使わない).
# 接続できるノードの数の上限を, 少なくして, 上限のテストをしやすくする.
target_compile_definitions(app PRIVATE CONFIG_BT_MAX_CONN=2)
# CONFIG_THERMO_SWITCHBOT_INTERVAL_MS は、アプリの Kconfig が定義する (ここでは、使わない)
target_compile_definitions(app PRIVATE CONFIG_THERMO_SWITCHBOT_INTERVAL_MS=1000)
target_sources(app PRIVATE
  src/test_ble.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/ble.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/switchbot.c)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
# モック (FFF の FAKE_VALUE_FUNC) は, ヘッダにある関数を, 同じ型で再宣言する
target_compile_options(app PRIVATE -Wno-redundant-decls)
