#include "test_framework.hpp"

#include <cstring>
#include <exception>

#ifdef _WIN32
#include <windows.h>
#endif

// Запуск: hammock_tests [подстрока имени теста]
int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0;
    int failed = 0;
    for (const auto& test : testing::registry()) {
        if (filter != nullptr && std::strstr(test.name, filter) == nullptr) continue;
        testing::failures() = 0;
        try {
            test.body();
        } catch (const testing::AbortTest&) {
        } catch (const std::exception& e) {
            testing::report(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        } catch (...) {
            testing::report(__FILE__, __LINE__, "unexpected unknown exception");
        }
        if (testing::failures() == 0) {
            ++passed;
            std::cout << "[  OK  ] " << test.name << "\n";
        } else {
            ++failed;
            std::cout << "[ FAIL ] " << test.name << "\n";
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed == 0 && passed > 0 ? 0 : 1;
}
