#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Process.h"

// Цепочка указателей.
struct PointerPath {
    std::string module; // откуда начинается цепочка
    uint64_t moduleOffset = 0; // смещение в модуле
    std::vector<int64_t> offsets; // смещения после каждого чтения

    bool operator==(const PointerPath& other) const;
};

// Текст -> цепочка, числа в hex.
std::optional<PointerPath> parsePointerPath(const std::string& text);

std::string formatPointerPath(const PointerPath& path);

// Модуль по имени, без учёта регистра.
const ModuleInfo* findModule(const std::vector<ModuleInfo>& modules, const std::string& name);

// В какой модуль попадает адрес.
const ModuleInfo* moduleContaining(const std::vector<ModuleInfo>& modules, uintptr_t address);

// Начало цепочки в этом запуске.
std::optional<uintptr_t> pathStart(const std::vector<ModuleInfo>& modules, const PointerPath& path);

// Идём по смещениям.
std::optional<uintptr_t> followOffsets(const Process& process, uintptr_t start, const std::vector<int64_t>& offsets);

// Адрес значения в этом запуске.
std::optional<uintptr_t> resolvePointer(const Process& process, const std::vector<ModuleInfo>& modules,
                                        const PointerPath& path);

void sortPathsByReliability(std::vector<PointerPath>& paths, const std::string& mainModule);

// Получаем число.
std::optional<int64_t> parseHex(const std::string& text);
