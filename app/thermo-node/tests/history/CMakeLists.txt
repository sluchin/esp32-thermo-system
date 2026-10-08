# thermo-node の history.c の単体テスト (Ztest). 関数だけで, 外のものを使わないので, モックはない.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_history_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src)
target_sources(app PRIVATE
  src/test_history.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/history.c)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
