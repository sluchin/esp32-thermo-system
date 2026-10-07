# thermo-node の sensor.c の単体テスト (Ztest). 3 つの構成を, TEST_MODE で切り替える.
#   simulator: シミュレーション値 (CONFIG_SIMULATOR=y. ADC なし)
#   adc      : ADC エミュレータ (zephyr,adc-emul) を, io-channels に接続した実機用の経路
#   adc_setup_error: adc と同じだが, チャンネルの設定が失敗する (sensor_init() のエラー経路)
cmake_minimum_required(VERSION 3.20.0)

if(NOT DEFINED TEST_MODE)
  set(TEST_MODE simulator)
endif()
if(NOT TEST_MODE STREQUAL "simulator" AND NOT TEST_MODE STREQUAL "adc"
   AND NOT TEST_MODE STREQUAL "adc_setup_error")
  message(FATAL_ERROR "TEST_MODE は simulator, adc, adc_setup_error のどれか: ${TEST_MODE}")
endif()

# アプリの Kconfig (CONFIG_SIMULATOR) を使う
set(KCONFIG_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../Kconfig)
set(CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/prj.conf;${CMAKE_CURRENT_LIST_DIR}/${TEST_MODE}.conf")
if(NOT TEST_MODE STREQUAL "simulator")
  set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/${TEST_MODE}.overlay)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_sensor_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_sources(app PRIVATE
  src/test_sensor_${TEST_MODE}.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/sensor.c)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
