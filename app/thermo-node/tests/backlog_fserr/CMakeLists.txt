# thermo-node の backlog.c の単体テスト: ファイルの操作 (fs_*) の失敗.
# fs_* を FFF のモックに置き換えて, 実物の FatFS では起こしにくい失敗 (書き込み, 閉じる, 移動, 読み出しなど) を作る.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_backlog_fserr_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/stub ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_compile_definitions(app PRIVATE CONFIG_THERMO_BACKLOG=1 CONFIG_THERMO_BACKLOG_MOUNT="/SD:"
                           CONFIG_THERMO_BACKLOG_MAX_RECORDS=5)
target_sources(app PRIVATE
  src/test_backlog_fserr.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/backlog.c
)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
# モック (FFF の FAKE_VALUE_FUNC) はヘッダにある関数を同じ型で再宣言する
target_compile_options(app PRIVATE -Wno-redundant-decls)
