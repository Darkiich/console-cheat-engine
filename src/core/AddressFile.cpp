#include "core/AddressFile.h"

#include <algorithm>
#include <fstream>

bool saveResults(const std::string& path, const ScanResults& results)
{
    std::ofstream out(path);
    if (!out)
        return false;

    const int typeNumber = static_cast<int>(results.type);
    const size_t size = valueSize(results.type);

    out << results.hits.size() << '\n';
    for (const ScanHit& hit : results.hits) {
        out << std::hex << hit.address << std::dec << ' '
            << typeNumber << ' ' << size << ' '
            << formatValue(results.type, hit.previous, /*roundTrip=*/true) << '\n';
    }
    return static_cast<bool>(out);
}

bool loadResults(const std::string& path, ScanResults& results, std::string& error)
{
    std::ifstream in(path);
    if (!in) {
        error = "file not found: " + path;
        return false;
    }

    size_t count = 0;
    if (!(in >> count)) {
        error = "failed to read file header";
        return false;
    }

    // Навсякий случай, если поверждён пишеим во временный объект.
    ScanResults loaded;
    bool typeKnown = false;

    for (size_t i = 0; i < count; i++) {
        const std::string lineInfo = "record " + std::to_string(i + 1) + ": ";

        unsigned long long address = 0;
        int typeNumber = 0;
        size_t size = 0;
        std::string valueText;
        if (!(in >> std::hex >> address >> std::dec >> typeNumber >> size >> valueText)) {
            error = lineInfo + "broken or missing";
            return false;
        }

        const std::optional<ValueType> type = valueTypeFromInt(typeNumber);
        if (!type || size != valueSize(*type)) {
            error = lineInfo + "unknown type";
            return false;
        }
        if (!typeKnown) {
            loaded.type = *type;
            typeKnown = true;
        } else if (*type != loaded.type) {
            error = lineInfo + "all records must have the same type";
            return false;
        }

        const std::optional<Value> value = parseValue(*type, valueText);
        if (!value) {
            error = lineInfo + "bad value '" + valueText + "'";
            return false;
        }

        loaded.hits.push_back({ static_cast<uintptr_t>(address), *value });
    }

    // сортируем и убираем дубликаты.
    auto byAddress = [](const ScanHit& a, const ScanHit& b) { return a.address < b.address; };
    auto sameAddress = [](const ScanHit& a, const ScanHit& b) { return a.address == b.address; };
    std::sort(loaded.hits.begin(), loaded.hits.end(), byAddress);
    loaded.hits.erase(std::unique(loaded.hits.begin(), loaded.hits.end(), sameAddress), loaded.hits.end());

    results = std::move(loaded);
    return true;
}
