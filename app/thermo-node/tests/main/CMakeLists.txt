# thermo-node の main.c の単体テスト (Ztest + FFF).
# sensor と ble の関数 (sensor_init() など) を, FFF のモックに置き換える.
# main.c の main() は, Zephyr のテストの main() と名前が同じなので, 名前を変える.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_main_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_sources(app PRIVATE
  src/test_main.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/main.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/boot_assets.c)
# 名前を変えた main() は, ヘッダに宣言がないので, その警告は出さない.
# CONFIG_THERMO_DISPLAY と CONFIG_THERMO_RTC は, OLED の初期化と表示 (oled.h の関数) と, RTC の時刻
# (node_time.h の関数) を, 有効にする. CONFIG_THERMO_BOOT_SCREEN と CONFIG_THERMO_BUZZER は, 起動画面と
# 起動音 (oled_show_boot() と buzzer.h の関数) を, 有効にする (どれもモック. 素材 boot_assets.c は, 本物)
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../src/main.c PROPERTIES
  COMPILE_DEFINITIONS "main=thermo_node_main;CONFIG_THERMO_DISPLAY=1;CONFIG_THERMO_RTC=1;CONFIG_THERMO_BOOT_SCREEN=1;CONFIG_THERMO_BUZZER=1;CONFIG_THERMO_GRAPH_STEP_S=30"
  COMPILE_OPTIONS "-Wno-missing-prototypes;-Wno-missing-declarations")

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
# モック (FFF の FAKE_VALUE_FUNC) は, ヘッダにある関数を, 同じ型で再宣言する
target_compile_options(app PRIVATE -Wno-redundant-decls)
