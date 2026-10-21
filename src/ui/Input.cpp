#include "ui/Input.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

std::string trim(const std::string& text)
{
    const char* spaces = " \t\r\n";
    const size_t begin = text.find_first_not_of(spaces);
    if (begin == std::string::npos)
        return "";
    const size_t end = text.find_last_not_of(spaces);
    return text.substr(begin, end - begin + 1);
}

// Строка -> число. Вся строка должна быть числом, иначе nullopt.
std::optional<long long> parseInteger(const std::string& text)
{
    if (text.empty())
        return std::nullopt;

    char* end = nullptr;
    errno = 0;
    const long long number = std::strtoll(text.c_str(), &end, 10);
    if (*end != '\0' || errno == ERANGE)
        return std::nullopt;
    return number;
}

} // namespace

std::string askLine(const char* prompt)
{
    std::fputs(prompt, stdout);
    std::fflush(stdout); // подссскассска

    std::string line;
    if (!std::getline(std::cin, line))
        throw InputClosed{};
    return trim(line);
}

long long askNumber(const char* prompt, long long min, long long max)
{
    for (;;) {
        const std::optional<long long> number = parseInteger(askLine(prompt));
        if (number && *number >= min && *number <= max)
            return *number;
        std::printf("Enter a number from %lld to %lld\n", min, max);
    }
}

std::optional<size_t> askIndex(size_t count)
{
    char prompt[96];
    std::snprintf(prompt, sizeof(prompt), "Enter number [1 - %zu] (empty = cancel): ", count);

    for (;;) {
        const std::string line = askLine(prompt);
        if (line.empty())
            return std::nullopt;

        const std::optional<long long> number = parseInteger(line);
        if (number && *number >= 1 && static_cast<unsigned long long>(*number) <= count)
            return static_cast<size_t>(*number - 1);
        std::puts("Invalid number");
    }
}

std::optional<Value> askValue(const std::string& prompt, ValueType type)
{
    for (;;) {
        const std::string line = askLine(prompt.c_str());
        if (line.empty())
            return std::nullopt;

        const std::optional<Value> value = parseValue(type, line);
        if (value)
            return value;
        std::printf("'%s' is not a valid %s value\n", line.c_str(), valueTypeName(type));
    }
}

std::optional<SearchValue> askSearchValue(const std::string& prompt, ValueType type)
{
    for (;;) {
        const std::string line = askLine(prompt.c_str());
        if (line.empty())
            return std::nullopt;

        const std::optional<SearchValue> value = parseSearchValue(type, line);
        if (value)
            return value;
        std::printf("'%s' is not a valid %s value\n", line.c_str(), valueTypeName(type));
    }
}

ValueType askValueType()
{
    std::puts("\nData type:");
    std::puts("1. short  (2 bytes)");
    std::puts("2. int    (4 bytes)");
    std::puts("3. float  (4 bytes)");
    std::puts("4. double (8 bytes)");
    std::puts("5. byte   (1 byte)");
    std::puts("6. int64  (8 bytes)");
    return *valueTypeFromInt(static_cast<int>(askNumber("Type: ", 1, kValueTypeCount)));
}
