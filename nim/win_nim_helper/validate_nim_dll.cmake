# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

function(validate_nim_dll_error text)
    if (NOT DESKTOP_APP_SPECIAL_TARGET STREQUAL "")
        message(FATAL_ERROR ${text})
    else()
        message(WARNING ${text})
    endif()
endfunction()

function(validate_nim_dll target_name)
    if (build_win64)
        set(modules_subdir x64)
    else()
        set(modules_subdir x86)
    endif()
    set(modules_debug_loc ${CMAKE_BINARY_DIR}/Debug/modules/${modules_subdir})
    set(modules_release_loc ${CMAKE_BINARY_DIR}/Release/modules/${modules_subdir})

    set(module_debug_path ${modules_debug_loc}/nim)
    set(module_release_path ${modules_release_loc}/nim)

    if (NOT EXISTS ${module_debug_path}/nim.dll
        OR NOT EXISTS ${module_release_path}/nim.dll)

        file(MAKE_DIRECTORY ${modules_debug_loc}/nim)
        file(COPY ${libs_loc}/nim_sdk/bin/nim.dll DESTINATION ${module_debug_path})
        file(COPY ${libs_loc}/nim_sdk/bin/h_available.dll DESTINATION ${module_debug_path})

        file(MAKE_DIRECTORY ${modules_release_loc}/nim)
        file(COPY ${libs_loc}/nim_sdk/bin/nim.dll DESTINATION ${module_release_path})
        file(COPY ${libs_loc}/nim_sdk/bin/h_available.dll DESTINATION ${module_release_path})


    endif()

endfunction()