# thermo-node の oled.c の単体テスト (Ztest + FFF).
# 表示装置は, 偽のもの (vnd,test-display. app.overlay) に置き換えて, 画素の形式の設定と, 表示の開始の
# 戻り値を, テストから設定する. CFB (文字のフレームバッファ) の関数 (cfb_print() など) は, FFF のモックに
# 置き換えて, 描く文字列と位置を確認する.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_oled_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_sources(app PRIVATE
  src/test_oled.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/oled.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/history.c)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
# モック (FFF の FAKE_VALUE_FUNC) は, ヘッダにある関数を, 同じ型で再宣言する
target_compile_options(app PRIVATE -Wno-redundant-decls)
