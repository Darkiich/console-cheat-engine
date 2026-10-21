#include "core/Pointer.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <tuple>

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

std::string toLower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::vector<std::string> splitArrows(const std::string& text)
{
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        const size_t arrow = text.find("->", start);
        parts.push_back(trim(text.substr(start, arrow == std::string::npos ? std::string::npos : arrow - start)));
        if (arrow == std::string::npos)
            break;
        start = arrow + 2;
    }
    return parts;
}

std::string formatHex(int64_t value)
{
    char buffer[32];
    if (value < 0)
        std::snprintf(buffer, sizeof(buffer), "-%llX", static_cast<unsigned long long>(-value));
    else
        std::snprintf(buffer, sizeof(buffer), "%llX", static_cast<unsigned long long>(value));
    return buffer;
}

} // namespace

bool PointerPath::operator==(const PointerPath& other) const
{
    return toLower(module) == toLower(other.module)
        && moduleOffset == other.moduleOffset
        && offsets == other.offsets;
}

std::optional<int64_t> parseHex(const std::string& input)
{
    std::string text = trim(input);
    bool negative = false;
    if (!text.empty() && text[0] == '-') {
        negative = true;
        text = trim(text.substr(1));
    }
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text = text.substr(2);
    if (text.empty())
        return std::nullopt;

    char* end = nullptr;
    errno = 0;
    const unsigned long long number = std::strtoull(text.c_str(), &end, 16);
    if (*end != '\0' || errno == ERANGE || number > 0x7FFFFFFFFFFFFFFFULL)
        return std::nullopt;

    const auto value = static_cast<int64_t>(number);
    return negative ? -value : value;
}

std::optional<PointerPath> parsePointerPath(const std::string& text)
{
    const std::vector<std::string> parts = splitArrows(text);

    const std::string& first = parts[0];
    const size_t plus = first.rfind('+');
    if (plus == std::string::npos || plus == 0)
        return std::nullopt;

    PointerPath path;
    path.module = trim(first.substr(0, plus));
    const std::optional<int64_t> moduleOffset = parseHex(first.substr(plus + 1));
    if (path.module.empty() || !moduleOffset || *moduleOffset < 0)
        return std::nullopt;
    path.moduleOffset = static_cast<uint64_t>(*moduleOffset);

    for (size_t i = 1; i < parts.size(); i++) {
        const std::optional<int64_t> offset = parseHex(parts[i]);
        if (!offset)
            return std::nullopt;
        path.offsets.push_back(*offset);
    }
    return path;
}

std::string formatPointerPath(const PointerPath& path)
{
    std::string text = path.module + "+" + formatHex(static_cast<int64_t>(path.moduleOffset));
    for (const int64_t offset : path.offsets)
        text += " -> " + formatHex(offset);
    return text;
}

const ModuleInfo* findModule(const std::vector<ModuleInfo>& modules, const std::string& name)
{
    const std::string wanted = toLower(name);
    for (const ModuleInfo& module : modules)
        if (toLower(module.name) == wanted)
            return &module;
    return nullptr;
}

const ModuleInfo* moduleContaining(const std::vector<ModuleInfo>& modules, uintptr_t address)
{
    for (const ModuleInfo& module : modules)
        if (address >= module.base && address - module.base < module.size)
            return &module;
    return nullptr;
}

std::optional<uintptr_t> pathStart(const std::vector<ModuleInfo>& modules, const PointerPath& path)
{
    const ModuleInfo* module = findModule(modules, path.module);
    if (!module)
        return std::nullopt;
    return module->base + static_cast<uintptr_t>(path.moduleOffset);
}

std::optional<uintptr_t> followOffsets(const Process& process, uintptr_t start, const std::vector<int64_t>& offsets)
{
    uintptr_t address = start;
    for (const int64_t offset : offsets) {
        uintptr_t pointer = 0;
        if (!process.readPointer(address, pointer) || pointer == 0)
            return std::nullopt;
        address = pointer + static_cast<uintptr_t>(offset);
    }
    return address;
}

std::optional<uintptr_t> resolvePointer(const Process& process, const std::vector<ModuleInfo>& modules,
                                        const PointerPath& path)
{
    const std::optional<uintptr_t> start = pathStart(modules, path);
    if (!start)
        return std::nullopt;
    return followOffsets(process, *start, path.offsets);
}

void sortPathsByReliability(std::vector<PointerPath>& paths, const std::string& mainModule)
{
    const std::string mainLower = toLower(mainModule);

    // чем меньше, тем надёжнее
    auto score = [&](const PointerPath& path) {
        uint64_t sum = 0;
        for (const int64_t offset : path.offsets)
            sum += static_cast<uint64_t>(offset < 0 ? -offset : offset);
        const bool fromGame = toLower(path.module) == mainLower;
        return std::make_tuple(fromGame ? 0 : 1, sum, path.offsets.size());
    };

    std::stable_sort(paths.begin(), paths.end(),
                     [&](const PointerPath& a, const PointerPath& b) { return score(a) < score(b); });
}
