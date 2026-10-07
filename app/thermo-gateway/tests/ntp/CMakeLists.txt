# thermo-gateway の ntp.c の単体テスト (Ztest + FFF).
# SNTP とソケットと時計の関数は, FFF のモックに置き換える (ネットワークスタックは使わない).
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_gateway_ntp_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_compile_definitions(app PRIVATE CONFIG_THERMO_NTP_RESYNC_S=3600)
target_sources(app PRIVATE
  src/test_ntp.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/ntp.c
)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
# モック (FFF の FAKE_VALUE_FUNC) は, ヘッダにある関数を, 同じ型で再宣言する
target_compile_options(app PRIVATE -Wno-redundant-decls)
