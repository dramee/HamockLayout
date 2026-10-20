#pragma once

// Минимальный тестовый фреймворк без внешних зависимостей.

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace testing {

struct TestCase {
    const char* name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

// Количество неуспешных проверок в текущем тесте.
inline int& failures() {
    static int count = 0;
    return count;
}

// Прерывание теста при неуспехе REQUIRE.
struct AbortTest {};

inline void report(const char* file, int line, const std::string& message) {
    ++failures();
    std::cerr << "  " << file << ":" << line << ": " << message << "\n";
}

template <typename T, typename = void>
struct IsStreamable : std::false_type {};
template <typename T>
struct IsStreamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <typename T>
std::string show(const T& value) {
    std::ostringstream out;
    if constexpr (std::is_same_v<T, std::string> || std::is_convertible_v<T, const char*>) {
        out << '"' << value << '"';
    } else if constexpr (IsStreamable<T>::value) {
        out << value;
    } else {
        out << "<value>";
    }
    return out.str();
}

}  // namespace testing

#define TEST_CONCAT_IMPL(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_IMPL(a, b)

#define TEST(name)                                                                    \
    static void name();                                                               \
    static const ::testing::Registrar TEST_CONCAT(registrar_, name)(#name, &name);    \
    static void name()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) ::testing::report(__FILE__, __LINE__, "CHECK(" #cond ") failed"); \
    } while (0)

#define REQUIRE(cond)                                                                   \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            ::testing::report(__FILE__, __LINE__, "REQUIRE(" #cond ") failed");         \
            throw ::testing::AbortTest{};                                               \
        }                                                                               \
    } while (0)

#define CHECK_EQ(actual, expected)                                                               \
    do {                                                                                         \
        const auto& a_ = (actual);                                                               \
        const auto& e_ = (expected);                                                             \
        if (!(a_ == e_)) {                                                                       \
            ::testing::report(__FILE__, __LINE__,                                                \
                              "CHECK_EQ(" #actual ", " #expected ") failed: " + ::testing::show(a_) + \
                                  " != " + ::testing::show(e_));                                 \
        }                                                                                        \
    } while (0)

#define CHECK_THROWS(expr, ExceptionType)                                                              \
    do {                                                                                               \
        bool caught_ = false;                                                                          \
        try {                                                                                          \
            (void)(expr);                                                                              \
        } catch (const ExceptionType&) {                                                               \
            caught_ = true;                                                                            \
        } catch (...) {                                                                                \
        }                                                                                              \
        if (!caught_) ::testing::report(__FILE__, __LINE__, "CHECK_THROWS(" #expr ", " #ExceptionType ") failed"); \
    } while (0)
