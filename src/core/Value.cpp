#include "core/Value.h"

#include <cerrno>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <limits>

std::optional<ValueType> valueTypeFromInt(int number)
{
    if (number >= 1 && number <= kValueTypeCount)
        return static_cast<ValueType>(number);
    return std::nullopt;
}

size_t valueSize(ValueType type)
{
    switch (type) {
    case ValueType::Short: return sizeof(int16_t);
    case ValueType::Int: return sizeof(int32_t);
    case ValueType::Float: return sizeof(float);
    case ValueType::Double: return sizeof(double);
    case ValueType::Byte: return sizeof(uint8_t);
    case ValueType::Int64: return sizeof(int64_t);
    }
    return 0;
}

std::optional<ValueType> valueTypeFromName(const std::string& name)
{
    for (int number = 1; number <= kValueTypeCount; number++) {
        const ValueType type = static_cast<ValueType>(number);
        if (name == valueTypeName(type))
            return type;
    }
    return std::nullopt;
}

const char* valueTypeName(ValueType type)
{
    switch (type) {
    case ValueType::Short: return "short";
    case ValueType::Int: return "int";
    case ValueType::Float: return "float";
    case ValueType::Double: return "double";
    case ValueType::Byte: return "byte";
    case ValueType::Int64: return "int64";
    }
    return "?";
}

namespace {

// Собираем целое число.
template <typename T>
std::optional<Value> parseInteger(const std::string& text)
{
    const char* begin = text.c_str();
    char* end = nullptr;
    errno = 0;
    const long long number = std::strtoll(begin, &end, 10);

    if (text.empty() || end == begin || *end != '\0' || errno == ERANGE)
        return std::nullopt;
    if constexpr (sizeof(T) < sizeof(long long)) {
        if (number < std::numeric_limits<T>::min() || number > std::numeric_limits<T>::max())
            return std::nullopt;
    }

    return Value::from(static_cast<T>(number));
}

// Запятые меняем на точки в дробных числах.
template <typename T>
std::optional<Value> parseFloating(std::string text)
{
    for (char& c : text)
        if (c == ',')
            c = '.';

    const char* begin = text.c_str();
    char* end = nullptr;
    errno = 0;
    const double number = std::strtod(begin, &end);

    if (text.empty() || end == begin || *end != '\0' || errno == ERANGE)
        return std::nullopt;
    if constexpr (std::is_same_v<T, float>) {
        if (std::fabs(number) > FLT_MAX)  // если не влезает во float
            return std::nullopt;
    }

    return Value::from(static_cast<T>(number));
}

} // namespace

std::optional<Value> parseValue(ValueType type, const std::string& text)
{
    switch (type) {
    case ValueType::Short: return parseInteger<int16_t>(text);
    case ValueType::Int: return parseInteger<int32_t>(text);
    case ValueType::Float: return parseFloating<float>(text);
    case ValueType::Double: return parseFloating<double>(text);
    case ValueType::Byte: return parseInteger<uint8_t>(text);
    case ValueType::Int64: return parseInteger<int64_t>(text);
    }
    return std::nullopt;
}

std::optional<SearchValue> parseSearchValue(ValueType type, const std::string& text)
{
    const std::optional<Value> value = parseValue(type, text);
    if (!value)
        return std::nullopt;

    SearchValue search;
    search.value = *value;
    if (type == ValueType::Float || type == ValueType::Double) {
        // сколько цифр после точки, столько и точность
        const size_t dot = text.find_first_of(".,");
        const size_t digits = dot == std::string::npos ? 0 : text.size() - dot - 1;
        search.tolerance = std::pow(10.0, -static_cast<double>(digits));
    }
    return search;
}

std::string formatValue(ValueType type, const Value& value, bool roundTrip)
{
    char buffer[64];
    switch (type) {
    case ValueType::Short:
        std::snprintf(buffer, sizeof(buffer), "%d", value.as<int16_t>());
        break;
    case ValueType::Int:
        std::snprintf(buffer, sizeof(buffer), "%d", value.as<int32_t>());
        break;
    case ValueType::Float:
        std::snprintf(buffer, sizeof(buffer), "%.*g", roundTrip ? 9 : 7, static_cast<double>(value.as<float>()));
        break;
    case ValueType::Double:
        std::snprintf(buffer, sizeof(buffer), "%.*g", roundTrip ? 17 : 15, value.as<double>());
        break;
    case ValueType::Byte:
        std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(value.as<uint8_t>()));
        break;
    case ValueType::Int64:
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value.as<int64_t>()));
        break;
    default:
        return "?";
    }
    return buffer;
}
