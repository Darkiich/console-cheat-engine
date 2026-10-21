#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>

// Тип искомого значения.
enum class ValueType : int {
    Short = 1, // 2 байта
    Int = 2, // 4 байта
    Float = 3, // 4 байта
    Double = 4, // 8 байт
    Byte = 5, // 1 байт
    Int64 = 6, // 8 байт
};

constexpr int kValueTypeCount = 6;

// Тип данных выбранный из меню. Возвращает значение в типе ValueType.
std::optional<ValueType> valueTypeFromInt(int number);

// Тип по названию ("int" -> Int), для файла таблицы.
std::optional<ValueType> valueTypeFromName(const std::string& name);

size_t valueSize(ValueType type); // Возвращает размер выбранного значения уже в байтах.
const char* valueTypeName(ValueType type); // Возвращает название типа данных - "int", "float"

// Значение любого из поддерживаемых типов - просто 8 байт для хранения.
struct Value {
    uint8_t bytes[8] = {};

    template <typename T>
    T as() const
    {
        static_assert(sizeof(T) <= sizeof(bytes), "type is too big for Value");
        T result;
        std::memcpy(&result, bytes, sizeof(T));
        return result;
    }

    template <typename T>
    static Value from(T value)
    {
        static_assert(sizeof(T) <= sizeof(bytes), "type is too big for Value");
        Value result;
        std::memcpy(result.bytes, &value, sizeof(T));
        return result;
    }
};

// Погрешность сравнения для дробных чисел.
constexpr double kFloatEpsilon = 0.001;

// Сравнение значений на равенство. Если значение дробное, то сравнивает с погрешностью, если нет, то не будет считать верхний код и сразу перейдёт к else из-за constexpr.
template <typename T>
bool valuesEqual(T a, T b)
{
    if constexpr (std::is_floating_point_v<T>)
        return std::fabs(static_cast<double>(a) - static_cast<double>(b)) < kFloatEpsilon;
    else
        return a == b;
}

// Разбирает ввод пользователя в значение нужного типа.
std::optional<Value> parseValue(ValueType type, const std::string& text);

// Что ищем.
struct SearchValue {
    Value value;
    double tolerance = 0.0;
};

std::optional<SearchValue> parseSearchValue(ValueType type, const std::string& text);

// Целые сравниваем точно, дробные с допуском.
template <typename T>
bool searchMatches(T value, const SearchValue& search)
{
    if constexpr (std::is_floating_point_v<T>)
        return std::fabs(static_cast<double>(value) - static_cast<double>(search.value.as<T>())) < search.tolerance;
    else
        return value == search.value.as<T>();
}

// Формируем строку из значения.
std::string formatValue(ValueType type, const Value& value, bool roundTrip = false);
