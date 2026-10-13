# thermo-node の direct.c の単体テスト (Ztest + FFF).
# ble と cfg と cloud と backlog と node_time の関数は FFF のモックに置き換える.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_direct_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
# SD カード (backlog.h の関数) はモックに置き換える (CONFIG_THERMO_BACKLOG=1 で, 関数の宣言を有効にする)
target_compile_definitions(app PRIVATE CONFIG_THERMO_BACKLOG=1)
target_sources(app PRIVATE
  src/test_direct.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/direct.c
)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
# モック (FFF の FAKE_VALUE_FUNC) はヘッダにある関数を同じ型で再宣言する
target_compile_options(app PRIVATE -Wno-redundant-decls)
