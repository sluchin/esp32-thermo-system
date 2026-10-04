# thermo-gateway の main.c の単体テスト (Ztest + FFF).
# ble と cloud の関数 (ble_init(), cloud_init() など) を, FFF のモックに置き換える.
# main.c の main() は, Zephyr のテストの main() と名前が同じなので, 名前を変える.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_gateway_main_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src)
# AWS IoT Core への送信 (cloud の関数は, FFF のモック). CONFIG_THERMO_CLOUD は, アプリの Kconfig が
# 定義するが, このテストは, アプリの Kconfig を使わないので, ここで定義する.
target_compile_definitions(app PRIVATE CONFIG_THERMO_CLOUD=1)
target_sources(app PRIVATE
  src/test_main.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/main.c)
# 名前を変えた main() は, ヘッダに宣言がないので, その警告は出さない
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../src/main.c PROPERTIES
  COMPILE_DEFINITIONS main=thermo_gateway_main
  COMPILE_OPTIONS "-Wno-missing-prototypes;-Wno-missing-declarations")

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
# モック (FFF の FAKE_VALUE_FUNC) は, ヘッダにある関数を, 同じ型で再宣言する
target_compile_options(app PRIVATE -Wno-redundant-decls)
