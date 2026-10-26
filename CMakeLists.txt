cmake_minimum_required(VERSION 3.16)
project(remmina-plugin-ultraviewer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(ZSTD REQUIRED libzstd)
pkg_check_modules(JPEG REQUIRED libjpeg)
pkg_check_modules(PULSE REQUIRED libpulse-simple)

find_path(REMMINA_INCLUDE_DIR remmina/plugin.h
    PATHS /usr/include /usr/local/include
)
if(NOT REMMINA_INCLUDE_DIR)
    message(FATAL_ERROR "Remmina plugin headers (remmina/plugin.h) not found. Please install remmina-devel or remmina.")
endif()

set(SOURCES
    src/uv_crypto.c
    src/uv_wire.c
    src/uv_tls.c
    src/uv_command.c
    src/uv_screen.c
    src/uv_cursor.c
    src/uv_clipboard.c
    src/uv_input.c
    src/uv_audio.c
    src/uv_file_transfer.c
    src/uv_session.c
    src/uv_plugin.c
)

add_library(remmina-plugin-ultraviewer MODULE ${SOURCES})

set_target_properties(remmina-plugin-ultraviewer PROPERTIES
    PREFIX ""
    OUTPUT_NAME "remmina-plugin-ultraviewer"
)

target_include_directories(remmina-plugin-ultraviewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${REMMINA_INCLUDE_DIR}
    ${GTK3_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${JPEG_INCLUDE_DIRS}
    ${PULSE_INCLUDE_DIRS}
)

target_compile_options(remmina-plugin-ultraviewer PRIVATE
    ${GTK3_CFLAGS_OTHER}
    -Wall
    -Wextra
    -Wno-unused-parameter
)

target_link_libraries(remmina-plugin-ultraviewer PRIVATE
    ${GTK3_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${JPEG_LIBRARIES}
    ${PULSE_LIBRARIES}
    m
    pthread
)

install(TARGETS remmina-plugin-ultraviewer
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/remmina/plugins"
)

install(DIRECTORY icons/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor"
    FILES_MATCHING PATTERN "*.svg" PATTERN "*.png"
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/certs/sectigo_roots.pem"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/remmina/ultraviewer"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/8bit.dat")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/8bit.dat"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/remmina/ultraviewer"
    )
endif()

enable_testing()
add_executable(test_ultraviewer
    tests/test_ultraviewer.c
    src/uv_crypto.c
    src/uv_wire.c
    src/uv_tls.c
    src/uv_input.c
    src/uv_clipboard.c
    src/uv_audio.c
    src/uv_file_transfer.c
    src/uv_command.c
)
target_include_directories(test_ultraviewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GTK3_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${PULSE_INCLUDE_DIRS}
)
target_link_libraries(test_ultraviewer PRIVATE
    ${GTK3_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${PULSE_LIBRARIES}
    m
    pthread
)
add_test(NAME test_ultraviewer COMMAND test_ultraviewer)
