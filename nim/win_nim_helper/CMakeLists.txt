# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

add_library(win_nim_helper STATIC)
init_target(win_nim_helper "(external)")
add_library(desktop-app::win_nim_helper ALIAS win_nim_helper)

nice_target_sources(win_nim_helper ${CMAKE_CURRENT_SOURCE_DIR}
PRIVATE
    win_nim_helper.cpp
)

include(validate_nim_dll.cmake)
validate_nim_dll(external_nim_sdk)

target_link_libraries(win_nim_helper
PUBLIC
    desktop-app::external_openssl
)

target_compile_definitions(win_nim_helper
PUBLIC
    ENABLE_NIM_V1=1
)
