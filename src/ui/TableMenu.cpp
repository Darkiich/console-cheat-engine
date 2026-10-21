#include "ui/TableMenu.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>

#include "ui/Input.h"

namespace {

unsigned long long asNumber(uintptr_t address)
{
    return static_cast<unsigned long long>(address);
}

void save(const Table& table)
{
    if (!saveTable(table))
        std::printf("Failed to save %s\n", tableFile(table.game).u8string().c_str());
}

bool requireEntries(const Table& table)
{
    if (table.entries.empty()) {
        std::puts("Table is empty. Find a value with a scan, then use 10 to save it here.");
        return false;
    }
    return true;
}

// Ищем значение, если нет - пишем почему.
std::optional<EntryLocation> locate(const Process& process, const std::vector<ModuleInfo>& modules,
                                    const TableEntry& entry)
{
    const std::optional<EntryLocation> location = locateEntry(process, modules, entry);
    if (!location)
        std::printf("'%s': can't find the value right now (is the game fully loaded?)\n", entry.name.c_str());
    return location;
}

void unfreeze(Freezer& freezer, const TableEntry& entry)
{
    for (const PointerPath& path : entry.paths)
        freezer.remove(path);
}

void actionChange(const Process& process, Freezer& freezer, Table& table)
{
    if (!requireEntries(table))
        return;
    printTable(process, freezer, table);
    const std::optional<size_t> index = askIndex(table.entries.size());
    if (!index)
        return;

    TableEntry& entry = table.entries[*index];
    const std::optional<EntryLocation> location = locate(process, process.modules(), entry);
    if (!location)
        return;

    const std::optional<Value> value = askValue("New value (empty = cancel): ", entry.type);
    if (!value)
        return;

    if (!process.writeValue(location->address, entry.type, *value)) {
        std::printf("Failed to write value. Error: %lu\n", GetLastError());
        return;
    }

    // заморожено - замораживаем новое значение
    if (isEntryFrozen(freezer, entry)) {
        for (const PointerPath& path : entry.paths)
            freezer.updateValue(path, *value);
        entry.freezeValue = *value;
        save(table);
    }
    std::printf("%s = %s\n", entry.name.c_str(), formatValue(entry.type, *value).c_str());
}

void actionToggleFreeze(const Process& process, Freezer& freezer, Table& table)
{
    if (!requireEntries(table))
        return;
    printTable(process, freezer, table);
    const std::optional<size_t> index = askIndex(table.entries.size());
    if (!index)
        return;

    TableEntry& entry = table.entries[*index];

    if (isEntryFrozen(freezer, entry)) {
        unfreeze(freezer, entry);
        entry.freezeValue.reset();
        save(table);
        std::printf("%s unfrozen\n", entry.name.c_str());
        return;
    }

    const std::vector<ModuleInfo> modules = process.modules();
    const std::optional<EntryLocation> location = locate(process, modules, entry);
    if (!location)
        return;
    const PointerPath& path = entry.paths[location->pathIndex];
    const std::optional<uintptr_t> start = pathStart(modules, path);
    if (!start)
        return;

    Value current;
    if (!process.readValue(location->address, entry.type, current)) {
        std::puts("Failed to read value");
        return;
    }

    const std::string prompt = "Freeze at value (empty = current " + formatValue(entry.type, current) + "): ";
    const std::optional<Value> wanted = askValue(prompt, entry.type);
    const Value value = wanted ? *wanted : current;
    if (wanted)
        process.writeValue(location->address, entry.type, value);

    // морозим по лучшей цепочке
    freezer.add({ location->address, entry.type, value, path, *start });
    entry.freezeValue = value; // в файл, чтобы в следующий раз заморозилось само
    save(table);
    std::printf("%s frozen at %s\n", entry.name.c_str(), formatValue(entry.type, value).c_str());
}

void actionAdd(Table& table, const Process& process, const ScanResults& results)
{
    std::puts("\nAdd entry:");
    std::puts("1. Enter pointer path (e.g. game.exe+1A2B3C -> 10 -> 7C)");
    std::puts("2. Static address from scan results");
    std::puts("0. Cancel");
    const long long choice = askNumber("Choice: ", 0, 2);

    if (choice == 1) {
        const std::optional<PointerPath> path = parsePointerPath(askLine("Pointer path: "));
        if (!path) {
            std::puts("Can't read this path. Format: module+offset -> offset -> offset (hex)");
            return;
        }
        addTableEntry(table, { *path }, askValueType());
        return;
    }

    if (choice == 2) {
        if (results.hits.empty()) {
            std::puts("No scan results");
            return;
        }

        const std::vector<ModuleInfo> modules = process.modules();
        const size_t shown = std::min<size_t>(100, results.hits.size());
        for (size_t i = 0; i < shown; i++) {
            const uintptr_t address = results.hits[i].address;
            const ModuleInfo* module = moduleContaining(modules, address);
            const std::string where = module ? formatPointerPath({ module->name, address - module->base, {} })
                                             : "dynamic (changes after restart)";
            std::printf("%4zu. 0x%012llX  %s\n", i + 1, asNumber(address), where.c_str());
        }

        const std::optional<size_t> index = askIndex(results.hits.size());
        if (!index)
            return;

        const uintptr_t address = results.hits[*index].address;
        const ModuleInfo* module = moduleContaining(modules, address);
        if (!module) {
            std::puts("This address is dynamic: it will be different after the game restarts.");
            std::puts("Use main menu 10 for it - it finds a pointer that survives restarts.");
            return;
        }
        addTableEntry(table, { PointerPath{ module->name, address - module->base, {} } }, results.type);
    }
}

void actionRename(const Process& process, const Freezer& freezer, Table& table)
{
    if (!requireEntries(table))
        return;
    printTable(process, freezer, table);
    const std::optional<size_t> index = askIndex(table.entries.size());
    if (!index)
        return;

    const std::string name = askLine("New name (empty = cancel): ");
    if (name.empty())
        return;
    if (name.find('|') != std::string::npos) {
        std::puts("Name can't contain '|'");
        return;
    }
    table.entries[*index].name = name;
    save(table);
}

void actionDelete(const Process& process, Freezer& freezer, Table& table)
{
    if (!requireEntries(table))
        return;
    printTable(process, freezer, table);
    const std::optional<size_t> index = askIndex(table.entries.size());
    if (!index)
        return;

    unfreeze(freezer, table.entries[*index]);
    std::printf("%s deleted\n", table.entries[*index].name.c_str());
    table.entries.erase(table.entries.begin() + static_cast<std::ptrdiff_t>(*index));
    save(table);
}

} // namespace

bool isEntryFrozen(const Freezer& freezer, const TableEntry& entry)
{
    for (const PointerPath& path : entry.paths)
        if (freezer.contains(path))
            return true;
    return false;
}

bool addTableEntry(Table& table, std::vector<PointerPath> paths, ValueType type)
{
    const std::string name = askLine("Name for this value, e.g. Money (empty = cancel): ");
    if (name.empty())
        return false;
    if (name.find('|') != std::string::npos) {
        std::puts("Name can't contain '|'");
        return false;
    }

    if (paths.size() > kMaxPathsPerEntry)
        paths.resize(kMaxPathsPerEntry);
    const size_t count = paths.size();

    table.entries.push_back({ name, type, std::move(paths), std::nullopt });
    if (!saveTable(table)) {
        std::printf("Failed to save %s\n", tableFile(table.game).u8string().c_str());
        return false;
    }
    std::printf("Saved '%s' (%zu pointer path%s). From now on it is found automatically - see menu 9.\n",
                name.c_str(), count, count == 1 ? "" : "s");
    return true;
}

void printTable(const Process& process, const Freezer& freezer, const Table& table)
{
    std::printf("\nTable for %s: %zu values\n", table.game.c_str(), table.entries.size());
    if (table.entries.empty())
        return;

    const std::vector<ModuleInfo> modules = process.modules();
    std::printf("   #  %-16s %-7s %-14s %-16s %s\n", "Name", "Type", "Value", "Address", "Pointer");
    for (size_t i = 0; i < table.entries.size(); i++) {
        const TableEntry& entry = table.entries[i];

        std::string valueText = "???";
        std::string addressText = "not found";
        std::string pointerText = entry.paths.empty() ? "" : formatPointerPath(entry.paths[0]);

        if (const std::optional<EntryLocation> location = locateEntry(process, modules, entry)) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "0x%012llX", asNumber(location->address));
            addressText = buffer;
            pointerText = formatPointerPath(entry.paths[location->pathIndex]);
            Value value;
            if (process.readValue(location->address, entry.type, value))
                valueText = formatValue(entry.type, value);
        }

        std::printf("%4zu. %-16s %-7s %-14s %-16s %s%s\n",
                    i + 1, entry.name.c_str(), valueTypeName(entry.type),
                    valueText.c_str(), addressText.c_str(), pointerText.c_str(),
                    isEntryFrozen(freezer, entry) ? "  [frozen]" : "");
    }
}

void applyTableFreezes(const Process& process, Freezer& freezer, const Table& table)
{
    const std::vector<ModuleInfo> modules = process.modules();
    for (const TableEntry& entry : table.entries) {
        if (!entry.freezeValue || entry.paths.empty())
            continue;

        const std::optional<EntryLocation> location = locateEntry(process, modules, entry);
        const PointerPath& path = entry.paths[location ? location->pathIndex : 0];
        const std::optional<uintptr_t> start = pathStart(modules, path);
        if (!start) {
            std::printf("'%s': module %s is not loaded, can't freeze\n", entry.name.c_str(), path.module.c_str());
            continue;
        }
        freezer.add({ location ? location->address : 0, entry.type, *entry.freezeValue, path, *start });
    }
}

void runTableMenu(const Process& process, Freezer& freezer, Table& table, const ScanResults& results)
{
    for (;;) {
        printTable(process, freezer, table);
        std::puts("\n1. Change value");
        std::puts("2. Freeze / unfreeze");
        std::puts("3. Add entry manually");
        std::puts("4. Rename");
        std::puts("5. Delete");
        std::puts("0. Back");

        switch (askNumber("Choice: ", 0, 5)) {
        case 0: return;
        case 1: actionChange(process, freezer, table); break;
        case 2: actionToggleFreeze(process, freezer, table); break;
        case 3: actionAdd(table, process, results); break;
        case 4: actionRename(process, freezer, table); break;
        case 5: actionDelete(process, freezer, table); break;
        default: break;
        }
    }
}
