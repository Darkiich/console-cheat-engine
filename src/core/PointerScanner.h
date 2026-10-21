#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/Pointer.h"
#include "core/Process.h"
#include "core/Value.h"

// Pointer scan
struct PointerScanSettings {
    int maxLevel = 4; // длина цепочки
    int64_t maxOffset = 0x1000; // смещение на каждом шаге
    int maxOffsetsPerNode = 4; // сколько ближайших указателей смотрим у адреса
    size_t maxResults = 100000; // больше не собираем
};

struct PointerScanStats {
    size_t bytesRead = 0; // сколько памяти прочитано
    size_t pointers = 0; // указателей в карте
    size_t nodes = 0; // сколько адресов проверено
    int levelsDone = 0; // до какого уровня дошли
    bool limited = false; // упёрлись в лимит
    double milliseconds = 0.0;
};

// Прогресс.
using ProgressCallback = std::function<void(const std::string&)>;

PointerScanStats pointerScan(const Process& process, uintptr_t target, const PointerScanSettings& settings,
                             std::vector<PointerPath>& out, const ProgressCallback& progress = {});
