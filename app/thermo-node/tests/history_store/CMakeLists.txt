# thermo-node の history_store.c の単体テスト (Ztest + FFF). RTC の時刻 (node_time_unix()) と, SD カードの
# 記録 (backlog_foreach_since()) は FFF のモック (CONFIG_THERMO_BACKLOG=1 は backlog.h の宣言を使うため).
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_history_store_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_compile_definitions(app PRIVATE CONFIG_THERMO_HISTORY_STORE=1 CONFIG_THERMO_BACKLOG=1
                           CONFIG_THERMO_BACKLOG_MAX_RECORDS=5)
target_sources(app PRIVATE
  src/test_history_store.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/history.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/history_store.c
)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
# モック (FFF の FAKE_VALUE_FUNC) はヘッダにある関数を同じ型で再宣言する
target_compile_options(app PRIVATE -Wno-redundant-decls)
