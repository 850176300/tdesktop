# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL
if (DESKTOP_APP_USE_PACKAGED)
    add_library(external_nim_wrapper_util INTERFACE IMPORTED GLOBAL)
    add_library(desktop-app::external_nim_wrapper_util ALIAS external_nim_wrapper_util)

    find_package(nim_sdk_wrapper REQUIRED)
    target_link_libraries(external_nim_wrapper_util INTERFACE nim_wrapper_util::nim_wrapper_util)
    return()
endif()


add_library(external_nim_wrapper_util STATIC IMPORTED GLOBAL)
add_library(desktop-app::external_nim_wrapper_util ALIAS external_nim_wrapper_util)

if (WIN32)
    set(nim_sdk_lib_loc ${libs_loc}/nim_sdk)
    set_target_properties(external_nim_wrapper_util PROPERTIES
        IMPORTED_LOCATION "${nim_sdk_lib_loc}/lib/nim_wrapper_util.lib"
        IMPORTED_LOCATION_DEBUG "${nim_sdk_lib_loc}/lib/nim_wrapper_util_d.lib"
    )
endif()