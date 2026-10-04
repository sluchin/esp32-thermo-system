# thermo-gateway の switchbot.c の単体テスト (Ztest).
# 解析と間引きは、Zephyr の関数を呼ばないので、モックは使わない.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_gateway_switchbot_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src)
# CONFIG_THERMO_SWITCHBOT_INTERVAL_MS は、アプリの Kconfig が定義するが、このテストは、
# アプリの Kconfig を使わないので、ここで定義する (送信の間隔: 1000 ms)
target_compile_definitions(app PRIVATE CONFIG_THERMO_SWITCHBOT_INTERVAL_MS=1000)
target_sources(app PRIVATE
  src/test_switchbot.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/switchbot.c
)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
