# thermo-node の sdlog.c の単体テスト (Ztest). RAM ディスクの FatFS (本物のファイルシステム) を使う.
# SD カードの代わりに, native_sim の RAM ディスク (app.overlay の ramdisk0. ディスク名 RAM) を, /RAM: にマウントする.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_sdlog_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_compile_definitions(app PRIVATE CONFIG_THERMO_SDLOG=1 CONFIG_THERMO_BACKLOG_MOUNT="/RAM:"
                           CONFIG_THERMO_SDLOG_BOOT_SEC=60 CONFIG_THERMO_SDLOG_BOOT_BUF_SIZE=512
                           CONFIG_THERMO_SDLOG_ERROR_BUF_SIZE=256
                           CONFIG_THERMO_SDLOG_ERROR_MAX_BYTES=1024)
target_sources(app PRIVATE
  src/test_sdlog.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/sdlog.c
)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
