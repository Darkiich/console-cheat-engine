#pragma once
#include <cstddef>
#include <optional>
#include <string>

#include "core/Value.h"

// Штука для безопасного ввода с потока.

// Бросается, когда ввод закончился. прога ловит и завершает программу.
struct InputClosed {};

// Читает строку целиком, обрезает пробелы.
std::string askLine(const char* prompt);

// Проверка числа в диапазоне.
long long askNumber(const char* prompt, long long min, long long max);

// Получаем индекс из диапазона
std::optional<size_t> askIndex(size_t count);

// Значение указанного типа.
std::optional<Value> askValue(const std::string& prompt, ValueType type);
