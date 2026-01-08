# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

macro(add_nim_subdirectory name)
    if (NOT DEFINED desktop_app_skip_libs
        OR NOT ${name} IN_LIST desktop_app_skip_libs)
        add_subdirectory(${name})
    endif()
endmacro()


add_nim_subdirectory(nim_sdk)
add_nim_subdirectory(nim_cpp_wrapper)
add_nim_subdirectory(nim_wrapper_util)
add_nim_subdirectory(win_nim_helper)
