# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL
if (DESKTOP_APP_USE_PACKAGED)
    add_library(external_nim_cpp_wrapper INTERFACE IMPORTED GLOBAL)
    add_library(desktop-app::external_nim_cpp_wrapper ALIAS external_nim_cpp_wrapper)

    find_package(nim_sdk_wrapper REQUIRED)
    target_link_libraries(external_nim_cpp_wrapper INTERFACE nim_cpp_wrapper::nim_cpp_wrapper)
    return()
endif()


add_library(external_nim_cpp_wrapper STATIC IMPORTED GLOBAL)
add_library(desktop-app::external_nim_cpp_wrapper ALIAS external_nim_cpp_wrapper)

if (WIN32)
    set(nim_sdk_lib_loc ${libs_loc}/nim_sdk)
    set_target_properties(external_nim_cpp_wrapper PROPERTIES
        IMPORTED_LOCATION "${nim_sdk_lib_loc}/lib/nim_cpp_wrapper.lib"
        IMPORTED_LOCATION_DEBUG "${nim_sdk_lib_loc}/lib/nim_cpp_wrapper_d.lib"
    )
endif()