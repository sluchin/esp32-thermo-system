# thermo-gateway の payload.c の単体テスト (Ztest + FFF).
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_gateway_payload_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_sources(app PRIVATE
  src/test_payload.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/payload.c
)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
# モック (FFF の FAKE_VALUE_FUNC) は, ヘッダにある関数を, 同じ型で再宣言する
target_compile_options(app PRIVATE -Wno-redundant-decls)
