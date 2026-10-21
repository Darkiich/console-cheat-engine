#include "core/Table.h"

#include <fstream>
#include <map>

#include "core/Win.h"

namespace {

std::string trim(const std::string& text)
{
    const char* spaces = " \t\r\n";
    const size_t begin = text.find_first_not_of(spaces);
    if (begin == std::string::npos)
        return "";
    const size_t end = text.find_last_not_of(spaces);
    return text.substr(begin, end - begin + 1);
}

// Режем строку по разделителю.
std::vector<std::string> split(const std::string& line, char separator)
{
    std::vector<std::string> columns;
    size_t start = 0;
    for (;;) {
        const size_t found = line.find(separator, start);
        columns.push_back(trim(line.substr(start, found == std::string::npos ? std::string::npos : found - start)));
        if (found == std::string::npos)
            break;
        start = found + 1;
    }
    return columns;
}

} // namespace

std::filesystem::path tablesDirectory()
{
    wchar_t exePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return std::filesystem::path(std::wstring(exePath, length)).parent_path() / "tables";
}

std::filesystem::path tableFile(const std::string& game)
{
    return tablesDirectory() / std::filesystem::u8path(game + ".txt");
}

bool tableExists(const std::string& game)
{
    std::error_code ignored;
    return std::filesystem::exists(tableFile(game), ignored);
}

// Формат: Money | int | game.exe+1A2B3C -> 10 -> 7C ; запасные цепочки | freeze 100
bool loadTable(const std::string& game, Table& table, std::string& error)
{
    Table loaded;
    loaded.game = game;

    std::ifstream in(tableFile(game));
    if (!in) {
        table = std::move(loaded); // файла нет - пустая таблица
        return true;
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        lineNumber++;
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        const std::vector<std::string> columns = split(line, '|');
        const std::string where = "line " + std::to_string(lineNumber) + ": ";
        if (columns.size() < 3) {
            error = where + "expected 'name | type | pointer'";
            return false;
        }

        TableEntry entry;
        entry.name = columns[0];

        const std::optional<ValueType> type = valueTypeFromName(columns[1]);
        if (!type) {
            error = where + "unknown type '" + columns[1] + "'";
            return false;
        }
        entry.type = *type;

        for (const std::string& text : split(columns[2], ';')) {
            const std::optional<PointerPath> path = parsePointerPath(text);
            if (!path) {
                error = where + "bad pointer '" + text + "'";
                return false;
            }
            entry.paths.push_back(*path);
        }

        // freeze <значение> - необязательно
        if (columns.size() >= 4 && columns[3].rfind("freeze ", 0) == 0) {
            const std::optional<Value> value = parseValue(entry.type, trim(columns[3].substr(7)));
            if (!value) {
                error = where + "bad freeze value";
                return false;
            }
            entry.freezeValue = *value;
        }

        loaded.entries.push_back(entry);
    }

    table = std::move(loaded);
    return true;
}

bool saveTable(const Table& table)
{
    std::error_code ignored;
    std::filesystem::create_directories(tablesDirectory(), ignored);

    std::ofstream out(tableFile(table.game));
    if (!out)
        return false;

    out << "# cce table: " << table.game << '\n';
    out << "# name | type | pointer | freeze <value> (optional)\n";
    for (const TableEntry& entry : table.entries) {
        out << entry.name << " | " << valueTypeName(entry.type) << " | ";
        for (size_t i = 0; i < entry.paths.size(); i++)
            out << (i > 0 ? " ; " : "") << formatPointerPath(entry.paths[i]);
        if (entry.freezeValue)
            out << " | freeze " << formatValue(entry.type, *entry.freezeValue, /*roundTrip=*/true);
        out << '\n';
    }
    return static_cast<bool>(out);
}

namespace {

// Похож ли адрес на значение.
bool plausibleAddress(const Process& process, uintptr_t address, ValueType type)
{
    if (address % valueSize(type) != 0)
        return false;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQueryEx(process.handle(), reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & writable) == 0 || (mbi.Protect & PAGE_GUARD) != 0)
        return false;

    Value ignored;
    return process.readValue(address, type, ignored);
}

} // namespace

std::optional<EntryLocation> locateEntry(const Process& process, const std::vector<ModuleInfo>& modules,
                                         const TableEntry& entry)
{
    std::map<uintptr_t, double> weight; // сумма весов
    std::map<uintptr_t, size_t> votes; // сколько цепочек
    std::map<uintptr_t, size_t> firstPath; // первая цепочка к адресу
    size_t working = 0;

    for (size_t i = 0; i < entry.paths.size(); i++) {
        const std::optional<uintptr_t> address = resolvePointer(process, modules, entry.paths[i]);
        if (!address || !plausibleAddress(process, *address, entry.type))
            continue;
        working++;
        weight[*address] += 1.0 / static_cast<double>(i + 1);
        votes[*address]++;
        firstPath.emplace(*address, i);
    }
    if (weight.empty())
        return std::nullopt;

    auto best = weight.begin();
    for (auto it = weight.begin(); it != weight.end(); ++it)
        if (it->second > best->second)
            best = it;

    const uintptr_t address = best->first;
    return EntryLocation{ address, firstPath[address], votes[address], working };
}
