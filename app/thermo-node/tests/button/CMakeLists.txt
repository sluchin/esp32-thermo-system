# thermo-node の button.c の単体テスト (Ztest).
# GPIO のコントローラを, 偽のもの (vnd,test-gpio. app.overlay) に置き換える. 偽のコントローラは,
# ピンの設定の戻り値と, ピンの入力の値を, テストから設定できる.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_button_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_sources(app PRIVATE
  src/test_button.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/button.c)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
