# thermo-node の backlog.c の単体テスト: SD カードがない (マウントできない) とき.
# マウントポイントを存在しないディスクの名前 (/NODISK:. RAM ディスクの名前は RAM) にして, マウントの失敗を作る.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_backlog_nodisk_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_compile_definitions(app PRIVATE CONFIG_THERMO_BACKLOG=1 CONFIG_THERMO_BACKLOG_MOUNT="/NODISK:"
                           CONFIG_THERMO_BACKLOG_MAX_RECORDS=5)
target_sources(app PRIVATE
  src/test_backlog_nodisk.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/backlog.c
)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
