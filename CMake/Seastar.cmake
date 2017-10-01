find_package(PkgConfig REQUIRED)
IF(CMAKE_BUILD_TYPE MATCHES Debug)
  set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:${PROJECT_SOURCE_DIR}/meta/tmp/seastar/build/debug")
ELSE()
  set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:${PROJECT_SOURCE_DIR}/meta/tmp/seastar/build/release")
ENDIF()
pkg_search_module(SEASTAR REQUIRED seastar)
include_directories(SYSTEM ${SEASTAR_INCLUDE_DIRS})

MESSAGE(STATUS "SEASTAR SEASTAR_FOUND: " "${SEASTAR_FOUND}")
MESSAGE(STATUS "SEASTAR SEASTAR_LIBRARIES: " "${SEASTAR_LIBRARIES}")
MESSAGE(STATUS "SEASTAR SEASTAR_LIBRARY_DIRS: " "${SEASTAR_LIBRARY_DIRS}")
MESSAGE(STATUS "SEASTAR SEASTAR_LDFLAGS: " "${SEASTAR_LDFLAGS}")
MESSAGE(STATUS "SEASTAR SEASTAR_LDFLAGS_OTHER: " "${SEASTAR_LDFLAGS_OTHER}")
MESSAGE(STATUS "SEASTAR SEASTAR_INCLUDE_DIRS: " "${SEASTAR_INCLUDE_DIRS}")
MESSAGE(STATUS "SEASTAR SEASTAR_CFLAGS: " "${SEASTAR_CFLAGS}")
MESSAGE(STATUS "SEASTAR CFLAGS_OTHER: " "${SEASTAR_CFLAGS_OTHER}")
