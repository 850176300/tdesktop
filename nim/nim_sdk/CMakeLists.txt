# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL
if (DESKTOP_APP_USE_PACKAGED)
    add_library(external_nim_sdk INTERFACE IMPORTED GLOBAL)
    add_library(desktop-app::external_nim_sdk ALIAS external_nim_sdk)

    find_package(nim_sdk REQUIRED)
    target_link_libraries(external_nim_sdk INTERFACE nim_sdk::nim_sdk)
    return()
endif()


add_library(external_nim_sdk STATIC IMPORTED GLOBAL)
add_library(desktop-app::external_nim_sdk ALIAS external_nim_sdk)

if (WIN32)
    set(nim_sdk_lib_loc ${libs_loc}/nim_sdk)
    target_include_directories(external_nim_sdk SYSTEM INTERFACE ${nim_sdk_lib_loc}/include)
    set_target_properties(external_nim_sdk PROPERTIES
        IMPORTED_LOCATION "${nim_sdk_lib_loc}/lib/nim.lib"
        IMPORTED_LOCATION_DEBUG "${nim_sdk_lib_loc}/lib/nim.lib"
    )
endif()