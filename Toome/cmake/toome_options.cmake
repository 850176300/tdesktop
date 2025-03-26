# This file is part of Toome Desktop,
# the official desktop application for the Toome messaging service.
#
# For license and copyright information please follow this link:

option(TDESKTOP_API_TEST "Use test API credentials." OFF)

if (DESKTOP_APP_DISABLE_AUTOUPDATE)
    target_compile_definitions(Toome PRIVATE TDESKTOP_DISABLE_AUTOUPDATE)
endif()

if (DESKTOP_APP_DISABLE_CRASH_REPORTS)
    target_compile_definitions(Toome PRIVATE TDESKTOP_DISABLE_CRASH_REPORTS)
endif()

if (DESKTOP_APP_USE_PACKAGED)
    target_compile_definitions(Toome PRIVATE TDESKTOP_USE_PACKAGED)
endif()

if (DESKTOP_APP_SPECIAL_TARGET)
    target_compile_definitions(Toome PRIVATE TDESKTOP_ALLOW_CLOSED_ALPHA)
endif()
