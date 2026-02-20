#pragma once

#include <string>
#include <string_view>
#include <optional>

template <typename T>
struct DefaultConstructedFunction;

template <typename Return, typename... Args>
struct DefaultConstructedFunction<Return(Args...)> {
    static Return value(Args...) { }
};

template <typename Return, typename... Args>
struct DefaultConstructedFunction<Return(*)(Args...)> {
    static Return value(Args...) { }
};

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
