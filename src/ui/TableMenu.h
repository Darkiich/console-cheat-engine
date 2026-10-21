#pragma once
#include "core/Freezer.h"
#include "core/Pointer.h"
#include "core/Process.h"
#include "core/Scanner.h"
#include "core/Table.h"

void printTable(const Process& process, const Freezer& freezer, const Table& table);

// Заморозки из файла, сразу после подключения.
void applyTableFreezes(const Process& process, Freezer& freezer, const Table& table);

void runTableMenu(const Process& process, Freezer& freezer, Table& table, const ScanResults& results);

// Спрашиваем имя, добавляем в таблицу и сохраняем.
bool addTableEntry(Table& table, std::vector<PointerPath> paths, ValueType type);

// Заморожено ли значение.
bool isEntryFrozen(const Freezer& freezer, const TableEntry& entry);
