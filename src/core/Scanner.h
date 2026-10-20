#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/Process.h"
#include "core/Value.h"

// Найденный адрес и значение, которое было по нему на последнем скане.
struct ScanHit {
    uintptr_t address;
    Value previous;
};

// Результаты скана.
struct ScanResults {
    ValueType type = ValueType::Int;
    std::vector<ScanHit> hits;
};

// Просто пункты меню для Next Scan'а.
enum class NextScanKind : int {
    Increased = 1,
    Decreased = 2,
    Exact = 3,
    Changed = 4,
    Unchanged = 5,
};

// Маленькая аналитика для вывода после скана - сколько читали и сколько это заняло.
struct ScanStats {
    size_t regions = 0; // сколько регионов памяти просканировано для First Scan'а
    size_t bytes = 0; // сколько байт удалось прочитать
    size_t readCalls = 0; // сколько раз вызван ReadProcessMemory
    double milliseconds = 0.0;
};

ScanStats firstScan(const Process& process, ValueType type, const Value& target, ScanResults& out);

ScanStats nextScan(const Process& process, NextScanKind kind, const Value& compare, ScanResults& results);