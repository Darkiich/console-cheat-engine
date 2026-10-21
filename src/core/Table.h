#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/Pointer.h"
#include "core/Process.h"
#include "core/Value.h"

// Значение в таблице.
struct TableEntry {
    std::string name;
    ValueType type = ValueType::Int;
    std::vector<PointerPath> paths;
    std::optional<Value> freezeValue; // есть - замораживаем сразу при подключении
};

// Сколько цепочек храним на значение.
constexpr size_t kMaxPathsPerEntry = 16;

// Где сейчас значение.
struct EntryLocation {
    uintptr_t address;
    size_t pathIndex; // лучшая цепочка к этому адресу
    size_t agree; // сколько цепочек сюда ведут
    size_t working; // сколько вообще куда-то ведут
};

std::optional<EntryLocation> locateEntry(const Process& process, const std::vector<ModuleInfo>& modules,
                                         const TableEntry& entry);

struct Table {
    std::string game;
    std::vector<TableEntry> entries;
};

// Папка tables рядом с cce.exe.
std::filesystem::path tablesDirectory();
std::filesystem::path tableFile(const std::string& game);

bool tableExists(const std::string& game);

// Нет файла - пустая таблица.
bool loadTable(const std::string& game, Table& table, std::string& error);

bool saveTable(const Table& table);
