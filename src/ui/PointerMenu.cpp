#include "ui/PointerMenu.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/PointerScanner.h"
#include "ui/Input.h"
#include "ui/TableMenu.h"

namespace {

unsigned long long asNumber(uintptr_t address)
{
    return static_cast<unsigned long long>(address);
}

// Номер из результатов скана или адрес с 0x.
struct Target {
    uintptr_t address;
    ValueType type;
};

std::optional<Target> askTarget(const ScanResults& results)
{
    const std::string answer = askLine("Which address? Number from scan results (empty = cancel): ");
    if (answer.empty())
        return std::nullopt;

    // адрес целиком - тогда тип спрашиваем
    if (answer.size() > 2 && answer[0] == '0' && (answer[1] == 'x' || answer[1] == 'X')) {
        const std::optional<int64_t> address = parseHex(answer);
        if (!address || *address <= 0) {
            std::puts("Bad address");
            return std::nullopt;
        }
        return Target{ static_cast<uintptr_t>(*address), askValueType() };
    }

    if (results.snapshot) {
        std::puts("Too many scan results. Narrow them down with Next Scan first.");
        return std::nullopt;
    }

    char* end = nullptr;
    const unsigned long long number = std::strtoull(answer.c_str(), &end, 10);
    if (*end != '\0' || number < 1 || number > results.hits.size()) {
        if (results.hits.empty())
            std::puts("No scan results. First find the value with 1. First Scan / 2. Next Scan.");
        else
            std::printf("Enter a number from 1 to %zu\n", results.hits.size());
        return std::nullopt;
    }
    return Target{ results.hits[number - 1].address, results.type };
}

// Прогресс поверх прошлой строки.
void showProgress(const std::string& text)
{
    std::printf("\r%-78s", text.c_str());
    std::fflush(stdout);
}

std::vector<PointerPath> search(const Process& process, uintptr_t target, const PointerScanSettings& settings)
{
    std::vector<PointerPath> paths;
    pointerScan(process, target, settings, paths, showProgress);
    showProgress("");
    std::printf("\r");
    return paths;
}

} // namespace

void runPointerMenu(const Process& process, Table& table, const ScanResults& results)
{
    std::puts("\nSave a found value permanently: the program finds a pointer to it,");
    std::puts("and from then on finds the value by itself every time the game starts.");
    std::puts("Pick the REAL address: the one that changes the value in the game when you write to it.");

    const std::optional<Target> target = askTarget(results);
    if (!target)
        return;

    const std::string name = askLine("Name, e.g. Money (empty = cancel): ");
    if (name.empty())
        return;
    if (name.find('|') != std::string::npos) {
        std::puts("Name can't contain '|'");
        return;
    }

    std::printf("Searching for a pointer to 0x%012llX...\n", asNumber(target->address));
    std::vector<PointerPath> paths = search(process, target->address, PointerScanSettings{});

    // не нашли - ищем глубже
    if (paths.empty()) {
        std::puts("Nothing found with the quick search, trying a deeper one (can take longer)...");
        PointerScanSettings deep;
        deep.maxLevel = 6;
        deep.maxOffsetsPerNode = 8;
        paths = search(process, target->address, deep);
    }

    if (paths.empty()) {
        std::puts("Couldn't find a pointer to this address.");
        std::puts("Make sure it's the real value (not a copy shown on screen) and the game is fully loaded.");
        return;
    }

    // Первая цепочка самая надёжная, остальные запасные.
    sortPathsByReliability(paths, table.game);
    if (paths.size() > kMaxPathsPerEntry)
        paths.resize(kMaxPathsPerEntry);

    const std::string pointerText = formatPointerPath(paths[0]);
    table.entries.push_back({ name, target->type, std::move(paths), std::nullopt });
    if (!saveTable(table)) {
        std::printf("Failed to save %s\n", tableFile(table.game).u8string().c_str());
        table.entries.pop_back();
        return;
    }

    Value value;
    const std::string valueText = process.readValue(target->address, target->type, value)
        ? formatValue(target->type, value)
        : "?";
    std::printf("\nDone: %s = %s\n", name.c_str(), valueText.c_str());
    std::printf("Pointer: %s\n", pointerText.c_str());
    std::puts("Saved. Next time it is found automatically - see menu 9 (change / freeze).");
}
