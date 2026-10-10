# thermo-node の buzzer.c の単体テスト (Ztest).
# PWM のコントローラを, 偽のもの (vnd,test-pwm. app.overlay) に置き換える. 偽のコントローラは,
# 設定された周期とパルスを, 順に記録して, 失敗は, テストから設定する.
# システムのワークキューで, メロディを順に鳴らすので, 仮想の時間を進めて, 確認する.
cmake_minimum_required(VERSION 3.20.0)

# binding (thermo,buzzer) は, アプリの dts/bindings を使う
list(APPEND DTS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermo_node_buzzer_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src
                           ${CMAKE_CURRENT_LIST_DIR}/../../../common)
target_sources(app PRIVATE
  src/test_buzzer.c
  ${CMAKE_CURRENT_LIST_DIR}/../../src/buzzer.c)

# 警告オプション (app/warnings.txt). 警告が出たら修正する
include(${CMAKE_CURRENT_LIST_DIR}/../../../warnings.cmake)
