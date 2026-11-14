# app/common の cfg.c の単体テスト (Ztest + FFF).
# settings と TLS の認証情報の関数は FFF のモックに置き換える.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_common_cfg_test)
# strnlen は POSIX の関数. native_sim (glibc, -std=c99) ではこの定義がないと宣言が見えない
target_compile_definitions(app PRIVATE _POSIX_C_SOURCE=200809L)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../..)
target_sources(app PRIVATE
  src/test_cfg.c
  ${CMAKE_CURRENT_LIST_DIR}/../../cfg.c
)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
# モック (FFF の FAKE_VALUE_FUNC) はヘッダにある関数を同じ型で再宣言する
target_compile_options(app PRIVATE -Wno-redundant-decls)
