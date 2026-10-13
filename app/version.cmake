# app/version.cmake
#
# バージョン (リポジトリ直下の VERSION) と git のハッシュ (git describe --always --dirty) を
# app ターゲットのマクロ THERMO_VERSION と THERMO_GIT_HASH に渡す (app/common/thermo_version.h).
# ハッシュに -dirty が付いていたら, コミットしていない変更を含むビルド. git が使えないときは unknown.
# 使い方: project() と target_sources(app ...) の後で include する.
set(THERMO_REPO_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

set(THERMO_VERSION "unknown")
if(EXISTS ${THERMO_REPO_DIR}/VERSION)
  file(STRINGS ${THERMO_REPO_DIR}/VERSION THERMO_VERSION LIMIT_COUNT 1)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${THERMO_REPO_DIR}/VERSION)
endif()

set(THERMO_GIT_HASH "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --always --dirty --abbrev=12
    WORKING_DIRECTORY ${THERMO_REPO_DIR}
    OUTPUT_VARIABLE THERMO_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE THERMO_GIT_RESULT)
  if((THERMO_GIT_RESULT EQUAL 0) AND (NOT "${THERMO_GIT_DESCRIBE}" STREQUAL ""))
    set(THERMO_GIT_HASH ${THERMO_GIT_DESCRIBE})
  endif()
  # コミットや切り替えをしたら, cmake をやり直して, ハッシュを更新する
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --absolute-git-dir
    WORKING_DIRECTORY ${THERMO_REPO_DIR}
    OUTPUT_VARIABLE THERMO_GIT_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(EXISTS "${THERMO_GIT_DIR}/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${THERMO_GIT_DIR}/HEAD)
  endif()
  if(EXISTS "${THERMO_GIT_DIR}/index")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${THERMO_GIT_DIR}/index)
  endif()
endif()

message(STATUS "Thermo version: ${THERMO_VERSION} (git ${THERMO_GIT_HASH})")
target_compile_definitions(app PRIVATE
  THERMO_VERSION="${THERMO_VERSION}"
  THERMO_GIT_HASH="${THERMO_GIT_HASH}")
