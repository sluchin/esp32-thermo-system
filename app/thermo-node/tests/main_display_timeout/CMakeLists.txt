# thermo-node の main.c の OLED の自動消灯の単体テスト (Ztest + FFF).
# CONFIG_THERMO_DISPLAY_TIMEOUT_SEC=10 (ボタンを押さずに 10 秒たつと, OLED を消す) だけを確かめるので,
# RTC と WiFi と起動画面は無効にする (tests/main はそれらを有効にして, 消灯を無効にしている).
# ブザーとボタンの音は有効にする (点灯のための押下では音を鳴らさないことを確かめる. buzzer_play() はモック).
# 関数 (sensor_init() など) は FFF のモックに置き換える.
# main.c の main() は Zephyr のテストの main() と名前が同じなので, 名前を変える.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_main_display_timeout_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_sources(app PRIVATE
  src/test_main_display_timeout.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/main.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/boot_assets.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/button_assets.c)
# 名前を変えた main() はヘッダに宣言がないので, その警告は出さない.
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../src/main.c PROPERTIES
  COMPILE_DEFINITIONS "main=thermo_node_main;CONFIG_THERMO_DISPLAY=1;CONFIG_THERMO_BUZZER=1;CONFIG_THERMO_BUTTON_SOUND=1;CONFIG_THERMO_GRAPH_STEP_SEC=30;CONFIG_THERMO_DISPLAY_TIMEOUT_SEC=10"
  COMPILE_OPTIONS "-Wno-missing-prototypes;-Wno-missing-declarations")

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
# モック (FFF の FAKE_VALUE_FUNC) はヘッダにある関数を同じ型で再宣言する
target_compile_options(app PRIVATE -Wno-redundant-decls)
