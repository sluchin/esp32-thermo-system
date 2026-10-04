# app/warnings.cmake
#
# warnings.txt の警告オプションのうち, コンパイラが受け付けるものを, app ターゲット
# (アプリのソースだけ. Zephyr 本体は含まない) に付ける.
# 使い方: project() と target_sources(app ...) の後で include する.
include(CheckCCompilerFlag)

file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/warnings.txt WARNING_LINES)
set(WARNING_FLAGS "")
foreach(line IN LISTS WARNING_LINES)
  string(REGEX REPLACE "#.*" "" flag "${line}")
  string(STRIP "${flag}" flag)
  if(flag STREQUAL "")
    continue()
  endif()
  string(MAKE_C_IDENTIFIER "HAVE_WARNING${flag}" flag_var)
  check_c_compiler_flag("${flag}" ${flag_var})
  if(${flag_var})
    list(APPEND WARNING_FLAGS ${flag})
  endif()
endforeach()
target_compile_options(app PRIVATE ${WARNING_FLAGS})

# Zephyr のヘッダ (GNU 拡張や, 多くのマクロを使う) の警告は, アプリでは直せないので出さない.
# Zephyr の include ディレクトリを, システムヘッダとして扱う (-I と -isystem の両方で
# 指定されたディレクトリは, システムヘッダとして扱われる).
get_target_property(ZEPHYR_INCLUDES zephyr_interface INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(ZEPHYR_SYSTEM_INCLUDES zephyr_interface INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
foreach(dirs ZEPHYR_INCLUDES ZEPHYR_SYSTEM_INCLUDES)
  if(${dirs})
    target_include_directories(app SYSTEM PRIVATE ${${dirs}})
  endif()
endforeach()

# 静的解析 (gcc -fanalyzer). 通常のビルドとは分けてあり, 解析に時間がかかる.
# cmake -DANALYZE=ON (docker compose run --rm analyze-thermo-node など) で, app のソースだけを解析する.
# 指摘 (警告) があれば, 失敗する. 最適化をすると, 不要なコードが消えて, 指摘できなくなることが
# あるので, -O0 で解析する.
option(ANALYZE "gcc -fanalyzer で, app のソースを静的解析する (指摘があれば失敗)" OFF)
if(ANALYZE)
  target_compile_options(app PRIVATE -O0 -fanalyzer -Werror)
endif()
