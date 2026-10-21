#pragma once
#include "core/Process.h"
#include "core/Scanner.h"
#include "core/Table.h"

// Ищем цепочку к адресу и пишем в таблицу.
void runPointerMenu(const Process& process, Table& table, const ScanResults& results);
