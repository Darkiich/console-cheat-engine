#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Process.h"
#include "core/Value.h"

// Найденный адрес и значение, которое было по нему на последнем скане.
struct ScanHit {
    uintptr_t address;
    Value previous;
};

class MemorySnapshot;

// Результаты скана.
struct ScanResults {
    ValueType type = ValueType::Int;
    std::vector<ScanHit> hits;
    std::shared_ptr<MemorySnapshot> snapshot;

    size_t count() const;
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
    std::string error; // не пусто, если не получилось
};

ScanStats firstScan(const Process& process, ValueType type, const SearchValue& target, ScanResults& out);

// Unknown initial value - запоминаем всю память.
ScanStats firstScanUnknown(const Process& process, ValueType type, ScanResults& out);

ScanStats nextScan(const Process& process, NextScanKind kind, const SearchValue& compare, ScanResults& results);
