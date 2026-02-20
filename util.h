#pragma once

#include <string>
#include <string_view>
#include <optional>

namespace util {

template <typename T>
struct DefaultConstructedFunction;

template <typename... Args>
struct DefaultConstructedFunction<void(Args...)> {
    static constexpr void value(Args...) { }
};

template <typename... Args>
struct DefaultConstructedFunction<void(*)(Args...)> : DefaultConstructedFunction<void(Args...)> { };

consteval void test_default_constructed_function() {
    DefaultConstructedFunction<void(int, char, bool)>::value(1, 'x', true);
    DefaultConstructedFunction<void(*)(int, char, bool)>::value(5, 'o', false);
}

template <typename T>
class StringSwitch {
    const std::string_view m_string;
    std::optional<T> m_value;
    T m_default{};

public:
    constexpr StringSwitch(std::string_view string) : m_string(string) { }

    constexpr StringSwitch& case_(std::string_view query, T value) {
        if (query == m_string)
            m_value = value;

        return *this;
    }

    constexpr StringSwitch& default_(T value) {
        m_default = value;

        return *this;
    }

    constexpr operator T() const {
        return done();
    }

    [[nodiscard]] constexpr T done() const {
        return m_value.value_or(m_default);
    }

};

consteval void test_string_switch() {

    static_assert(StringSwitch<int>("foo")
    .case_("foo", 1)
    .case_("bar", 2)
    .case_("baz", 3)
    == 1);

    static_assert(StringSwitch<int>("foo")
    .case_("bar", 2)
    .case_("baz", 3)
    .default_(1)
    == 1);

    static_assert(StringSwitch<int>("foo")
    .case_("bar", 2)
    .case_("baz", 3)
    == 0);

}

template <typename... Ts>
struct OverloadedLambda : Ts... {
    using Ts::operator()...;
};

consteval void test_overloaded_lambda() {
    static_assert(OverloadedLambda {
        [](int) { return 1; },
        [](double) { return 5.0; },
        [](char) { return 'x'; },
    }(1) == 1);

    static_assert(OverloadedLambda {
        [](std::string) { return "foo"; },
        [](const char*) { return 5.0; },
        [](int) { return 1; },
    }("hello") == 5.0);
}

} // namespace util
