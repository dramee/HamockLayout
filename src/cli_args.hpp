#pragma once

// Аргументы командной строки в UTF-8. На Windows argv приходит в кодировке
// ANSI, и кириллица в параметрах (например, в заголовке) искажается, поэтому
// аргументы берутся из GetCommandLineW и перекодируются.

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

inline std::vector<std::string> utf8Arguments(int argc, char** argv) {
#ifdef _WIN32
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide != nullptr) {
        std::vector<std::string> args;
        for (int i = 0; i < count; ++i) {
            const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string arg(size > 0 ? size - 1 : 0, '\0');
            if (size > 1) WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, arg.data(), size, nullptr, nullptr);
            args.push_back(std::move(arg));
        }
        LocalFree(wide);
        return args;
    }
#endif
    return std::vector<std::string>(argv, argv + argc);
}
