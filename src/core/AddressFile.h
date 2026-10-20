#pragma once
#include <string>

#include "core/Scanner.h"

// Сохранение/загрузка результатов скана.

bool saveResults(const std::string& path, const ScanResults& results);

bool loadResults(const std::string& path, ScanResults& results, std::string& error);
