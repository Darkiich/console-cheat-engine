#include "ui/Menu.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "core/AddressFile.h"
#include "core/Freezer.h"
#include "core/Scanner.h"
#include "ui/Input.h"

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
    if (s.results.hits.empty()) {
        std::puts("No results. Run First Scan (or load addresses) first.");
        return false;
    }
    return true;
}

void actionFirstScan(Session& s)
{
    std::puts("\nData type:");
    std::puts("1. short  (2 bytes)");
    std::puts("2. int    (4 bytes)");
    std::puts("3. float  (4 bytes)");
    std::puts("4. double (8 bytes)");
    const ValueType type = *valueTypeFromInt(static_cast<int>(askNumber("Type: ", 1, 4)));

    const std::optional<Value> target = askValue("Search value (empty = cancel): ", type);
    if (!target)
        return;

    std::puts("Scanning...");
    const ScanStats stats = firstScan(s.process, type, *target, s.results);
    printStats("First Scan", stats, s.results.hits.size());
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

    Value compare;
    if (kind == NextScanKind::Exact) {
        const std::optional<Value> value = askValue("New value (empty = cancel): ", s.results.type);
        if (!value)
            return;
        compare = *value;
    }

    const size_t before = s.results.hits.size();
    const ScanStats stats = nextScan(s.process, kind, compare, s.results);
    std::printf("%zu -> %zu\n", before, s.results.hits.size());
    printStats("Next Scan", stats, s.results.hits.size());

    if (!s.results.hits.empty() && s.results.hits.size() <= kAutoShowLimit)
        printResults(s);
}

void actionChangeValue(Session& s)
{
    if (!requireResults(s))
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
    if (!requireResults(s))
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

    s.freezer.add({ hit.address, type, value });
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
    std::puts("Value unfrozen");
}

void actionShowResults(Session& s)
{
    if (!requireResults(s))
        return;
    printResults(s);
}

void actionSave(Session& s)
{
    if (!requireResults(s))
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
    std::printf("\n[results: %zu %s | frozen: %zu]\n",
                s.results.hits.size(), valueTypeName(s.results.type), s.freezer.entries().size());
    std::puts("1. First Scan");
    std::puts("2. Next Scan");
    std::puts("3. Change value");
    std::puts("4. Freeze value");
    std::puts("5. Unfreeze value");
    std::puts("6. Show found addresses");
    std::puts("7. Save addresses to file");
    std::puts("8. Load addresses from file");
    std::puts("0. Exit");
}

} // namespace

void runMenu(const Process& process, const std::string& saveFile)
{
    Session session(process, saveFile);

    for (;;) {
        if (!process.isAlive()) {
            std::puts("\nTarget process has exited.");
            return;
        }

        printMenu(session);
        switch (askNumber("Choice: ", 0, 8)) {
        case 0: return;
        case 1: actionFirstScan(session); break;
        case 2: actionNextScan(session); break;
        case 3: actionChangeValue(session); break;
        case 4: actionFreezeValue(session); break;
        case 5: actionUnfreezeValue(session); break;
        case 6: actionShowResults(session); break;
        case 7: actionSave(session); break;
        case 8: actionLoad(session); break;
        default: break;
        }
    }
}
