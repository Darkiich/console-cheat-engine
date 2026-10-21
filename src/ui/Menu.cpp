#include "ui/Menu.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "core/AddressFile.h"
#include "core/Freezer.h"
#include "core/Scanner.h"
#include "core/Table.h"
#include "ui/Input.h"
#include "ui/PointerMenu.h"
#include "ui/TableMenu.h"

namespace {

// Сколько адресов максимум печатать в списках.
constexpr size_t kMaxShown = 100;

// Печатает список после next scan сам, если адресов осталось не больше этого числа.
constexpr size_t kAutoShowLimit = 20;

// Всё состояние сессии работы с процессом.
struct Session {
    const Process& process;
    ScanResults results;
    Freezer freezer;  // останавливание потока заморозки да.
    std::string saveFile;
    Table table; // значения этой игры

    Session(const Process& p, std::string file)
        : process(p), freezer(p), saveFile(std::move(file))
    {
    }
};

unsigned long long asNumber(uintptr_t address)
{
    return static_cast<unsigned long long>(address);
}

void printStats(const char* title, const ScanStats& stats, size_t found)
{
    std::printf("%s done: %zu results | read %.1f MB in %zu calls | %.0f ms\n",
                title, found,
                static_cast<double>(stats.bytes) / (1024.0 * 1024.0),
                stats.readCalls, stats.milliseconds);
}

void printResults(const Session& s)
{
    const ScanResults& r = s.results;
    const size_t shown = std::min(kMaxShown, r.hits.size());

    std::printf("Type: %s | results: %zu\n", valueTypeName(r.type), r.hits.size());
    std::printf("   #  Address           Current          Previous\n");
    for (size_t i = 0; i < shown; i++) {
        const ScanHit& hit = r.hits[i];

        Value current;
        const std::string currentText = s.process.readValue(hit.address, r.type, current)
            ? formatValue(r.type, current)
            : "???";

        std::printf("%4zu. 0x%012llX  %-16s %-16s%s\n",
                    i + 1, asNumber(hit.address),
                    currentText.c_str(),
                    formatValue(r.type, hit.previous).c_str(),
                    s.freezer.contains(hit.address) ? "[frozen]" : "");
    }
    if (shown < r.hits.size())
        std::printf("... and %zu more. Narrow the results with Next Scan.\n", r.hits.size() - shown);
}

bool requireResults(const Session& s)
{
    if (s.results.count() == 0) {
        std::puts("No results. Run First Scan (or load addresses) first.");
        return false;
    }
    return true;
}

bool requireList(const Session& s)
{
    if (!requireResults(s))
        return false;
    if (s.results.snapshot) {
        std::printf("%zu addresses is too many. Narrow them down with Next Scan first.\n", s.results.count());
        return false;
    }
    return true;
}

void actionFirstScan(Session& s)
{
    const ValueType type = askValueType();

    std::puts("\n1. Exact value");
    std::puts("2. Unknown initial value");
    const long long mode = askNumber("Choice: ", 1, 2);

    if (mode == 2) {
        std::puts("Remembering all memory (temporary file in %TEMP%)...");
        const ScanStats stats = firstScanUnknown(s.process, type, s.results);
        if (!stats.error.empty()) {
            std::printf("Unknown initial value failed: %s\n", stats.error.c_str());
            return;
        }
        printStats("First Scan", stats, s.results.count());
        std::puts("Now change the value in the game and use Next Scan (Increased / Decreased / Changed).");
        return;
    }

    const std::optional<SearchValue> target = askSearchValue("Search value (empty = cancel): ", type);
    if (!target)
        return;
    if (target->tolerance > 0)
        std::printf("Looking for %s +- %g\n", formatValue(type, target->value).c_str(), target->tolerance);

    std::puts("Scanning...");
    const ScanStats stats = firstScan(s.process, type, *target, s.results);
    printStats("First Scan", stats, s.results.count());
    std::printf("Scanned %zu memory regions\n", stats.regions);
}

void actionNextScan(Session& s)
{
    if (!requireResults(s))
        return;

    std::puts("\nNext Scan type:");
    std::puts("1. Increased");
    std::puts("2. Decreased");
    std::puts("3. Exact Value");
    std::puts("4. Changed");
    std::puts("5. Unchanged");
    const auto kind = static_cast<NextScanKind>(askNumber("Choice: ", 1, 5));

    SearchValue compare;
    if (kind == NextScanKind::Exact) {
        const std::optional<SearchValue> value = askSearchValue("New value (empty = cancel): ", s.results.type);
        if (!value)
            return;
        compare = *value;
    }

    if (s.results.snapshot)
        std::puts("Comparing with the remembered memory...");
    const size_t before = s.results.count();
    const ScanStats stats = nextScan(s.process, kind, compare, s.results);
    if (!stats.error.empty())
        std::printf("Next Scan failed: %s\n", stats.error.c_str());
    std::printf("%zu -> %zu\n", before, s.results.count());
    printStats("Next Scan", stats, s.results.count());

    if (!s.results.snapshot && !s.results.hits.empty() && s.results.hits.size() <= kAutoShowLimit)
        printResults(s);
}

void actionChangeValue(Session& s)
{
    if (!requireList(s))
        return;

    printResults(s);
    const std::optional<size_t> index = askIndex(s.results.hits.size());
    if (!index)
        return;

    const ScanHit hit = s.results.hits[*index];
    const ValueType type = s.results.type;

    const std::optional<Value> value = askValue("New value (empty = cancel): ", type);
    if (!value)
        return;

    if (!s.process.writeValue(hit.address, type, *value)) {
        std::printf("Failed to write value. Error: %lu\n", GetLastError());
        return;
    }

    const bool wasFrozen = s.freezer.updateValue(hit.address, *value);
    std::printf("Value %s written to 0x%012llX%s\n",
                formatValue(type, *value).c_str(), asNumber(hit.address),
                wasFrozen ? " (frozen value updated)" : "");
}

void actionFreezeValue(Session& s)
{
    if (!requireList(s))
        return;

    printResults(s);
    const std::optional<size_t> index = askIndex(s.results.hits.size());
    if (!index)
        return;

    const ScanHit hit = s.results.hits[*index];
    const ValueType type = s.results.type;

    if (s.freezer.contains(hit.address)) {
        std::puts("This address is already frozen");
        return;
    }

    Value current;
    if (!s.process.readValue(hit.address, type, current)) {
        std::puts("Failed to read value at this address");
        return;
    }

    const std::string prompt = "Freeze at value (empty = current " + formatValue(type, current) + "): ";
    const std::optional<Value> wanted = askValue(prompt, type);
    const Value value = wanted ? *wanted : current;

    if (wanted && !s.process.writeValue(hit.address, type, value)) {
        std::printf("Failed to write value. Error: %lu\n", GetLastError());
        return;
    }

    s.freezer.add({ hit.address, type, value, std::nullopt, 0 });
    std::printf("0x%012llX frozen at %s\n", asNumber(hit.address), formatValue(type, value).c_str());
}

void actionUnfreezeValue(Session& s)
{
    const std::vector<FrozenEntry> frozen = s.freezer.entries();
    if (frozen.empty()) {
        std::puts("Nothing is frozen");
        return;
    }

    std::puts("Frozen values:");
    for (size_t i = 0; i < frozen.size(); i++) {
        const FrozenEntry& e = frozen[i];
        std::printf("%4zu. 0x%012llX  %-6s %s\n",
                    i + 1, asNumber(e.address), valueTypeName(e.type), formatValue(e.type, e.value).c_str());
    }

    const std::optional<size_t> index = askIndex(frozen.size());
    if (!index)
        return;

    s.freezer.remove(frozen[*index].address);

    // из таблицы - убираем заморозку и в файле
    if (frozen[*index].path) {
        for (TableEntry& entry : s.table.entries)
            if (std::find(entry.paths.begin(), entry.paths.end(), *frozen[*index].path) != entry.paths.end())
                entry.freezeValue.reset();
        saveTable(s.table);
    }
    std::puts("Value unfrozen");
}

void actionShowResults(Session& s)
{
    if (!requireList(s))
        return;
    printResults(s);
}

void actionSave(Session& s)
{
    if (!requireList(s))
        return;

    if (saveResults(s.saveFile, s.results))
        std::printf("Saved %zu addresses to %s\n", s.results.hits.size(), s.saveFile.c_str());
    else
        std::printf("Failed to write %s\n", s.saveFile.c_str());
}

void actionLoad(Session& s)
{
    std::string error;
    if (loadResults(s.saveFile, s.results, error))
        std::printf("Loaded %zu addresses (%s) from %s\n",
                    s.results.hits.size(), valueTypeName(s.results.type), s.saveFile.c_str());
    else
        std::printf("Load failed: %s\n", error.c_str());
}

void printMenu(const Session& s)
{
    std::printf("\n[results: %zu %s | frozen: %zu | table: %zu]\n",
                s.results.count(), valueTypeName(s.results.type), s.freezer.entries().size(),
                s.table.entries.size());
    std::puts("1. First Scan");
    std::puts("2. Next Scan");
    std::puts("3. Change value");
    std::puts("4. Freeze value");
    std::puts("5. Unfreeze value");
    std::puts("6. Show found addresses");
    std::puts("7. Save addresses to file");
    std::puts("8. Load addresses from file");
    std::puts("9. Table of pointers scan");
    std::puts("10. Save found value permanently (pointer scan)");
    std::puts("0. Exit");
}

} // namespace

MenuExit runMenu(const Process& process, const std::string& saveFile)
{
    Session session(process, saveFile);

    // Есть таблица - сразу показываем значения и включаем заморозки.
    session.table.game = process.exeName();
    if (session.table.game.empty())
        session.table.game = "pid" + std::to_string(process.pid());
    std::string error;
    if (!loadTable(session.table.game, session.table, error))
        std::printf("Can't load table for %s: %s\n", session.table.game.c_str(), error.c_str());
    if (!session.table.entries.empty()) {
        applyTableFreezes(process, session.freezer, session.table);
        printTable(process, session.freezer, session.table);
    }

    for (;;) {
        if (!process.isAlive()) {
            std::puts("\nTarget process has exited.");
            return MenuExit::ProcessExited;
        }

        printMenu(session);
        switch (askNumber("Choice: ", 0, 10)) {
        case 0: return MenuExit::User;
        case 1: actionFirstScan(session); break;
        case 2: actionNextScan(session); break;
        case 3: actionChangeValue(session); break;
        case 4: actionFreezeValue(session); break;
        case 5: actionUnfreezeValue(session); break;
        case 6: actionShowResults(session); break;
        case 7: actionSave(session); break;
        case 8: actionLoad(session); break;
        case 9: runTableMenu(process, session.freezer, session.table, session.results); break;
        case 10: runPointerMenu(process, session.table, session.results); break;
        default: break;
        }
    }
}
