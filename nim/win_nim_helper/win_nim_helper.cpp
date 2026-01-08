// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//

#include <windows.h>
#include <string>
#include <vector>
#include <array>


namespace NimSdk{
    namespace{

        constexpr auto kMaxPathLong = 32767;

        bool ResolveNimSdkDll() {
            static const auto loaded = [] {
                auto exePath = std::array<WCHAR, kMaxPathLong + 1>{ { 0 } };
                const auto exeLength = GetModuleFileName(
                    nullptr,
                    exePath.data(),
                    kMaxPathLong + 1);
                if (!exeLength || exeLength >= kMaxPathLong + 1) {
                    return false;
                }
                const auto exe = std::wstring(exePath.data());
                const auto last1 = exe.find_last_of('\\');
                const auto last2 = exe.find_last_of('/');
                const auto last = max(
                    (last1 == std::wstring::npos) ? -1 : int(last1),
                    (last2 == std::wstring::npos) ? -1 : int(last2));
                if (last < 0) {
                    return false;
                }

                #if defined _WIN64
                    const auto arch = L"x64";
                #elif defined _WIN32 // _WIN64
                    const auto arch = L"x86";
                #else // _WIN64 || _WIN32
                    #error "Invalid configuration."
                #endif // _WIN64 || _WIN32
                const auto compiler = exe.substr(0, last + 1)
                    + L"modules\\" + arch + L"\\nim";
                const auto path = compiler.c_str();
                AddDllDirectory(path);
                return true;
            }();
            return loaded;
        }
    }
}

bool NimSdkResolveCompiler(){
    return NimSdk::ResolveNimSdkDll();
}