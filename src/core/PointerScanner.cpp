#include "core/PointerScanner.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <unordered_map>

namespace {

// Читаем кусками по 4 МБ, как в First Scan.
constexpr size_t kChunkSize = 4 * 1024 * 1024;

// Лимиты, чтобы поиск не съел всю память.
constexpr size_t kMaxNodes = 4'000'000;
constexpr size_t kMaxEdges = 8'000'000;

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Range {
    uintptr_t start;
    uintptr_t end;
};

// Есть ли адрес в диапазоне. Поиск через двоичный поиск.
bool insideRanges(const std::vector<Range>& ranges, uintptr_t address)
{
    auto it = std::upper_bound(ranges.begin(), ranges.end(), address,
                               [](uintptr_t value, const Range& r) { return value < r.start; });
    if (it == ranges.begin())
        return false;
    --it;
    return address < it->end;
}

std::vector<Range> collectRegions(const Process& process)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    uintptr_t address = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t maxAddress = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);

    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    std::vector<Range> regions;
    MEMORY_BASIC_INFORMATION mbi;

    while (address < maxAddress
           && VirtualQueryEx(process.handle(), reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize == 0)
            break;

        const bool usable = mbi.State == MEM_COMMIT
            && mbi.Type != MEM_MAPPED
            && (mbi.Protect & writable) != 0
            && (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;

        if (usable) {
            // соседние регионы склеиваем
            if (!regions.empty() && regions.back().end == base)
                regions.back().end = base + mbi.RegionSize;
            else
                regions.push_back({ base, base + mbi.RegionSize });
        }
        address = base + mbi.RegionSize;
    }
    return regions;
}

struct PointerRecord {
    uintptr_t value;
    uintptr_t address;
};

template <typename P>
void collectPointers(const uint8_t* data, size_t size, uintptr_t base, const std::vector<Range>& regions,
                     std::vector<PointerRecord>& out)
{
    const uintptr_t lowest = regions.front().start;
    const uintptr_t highest = regions.back().end;

    for (size_t offset = 0; offset + sizeof(P) <= size; offset += sizeof(P)) {
        P raw;
        std::memcpy(&raw, data + offset, sizeof(P));
        const uintptr_t value = static_cast<uintptr_t>(raw);

        // не адрес или не выровнено
        if (value < lowest || value >= highest || (value & 3) != 0)
            continue;
        if (!insideRanges(regions, value))
            continue;

        out.push_back({ value, base + offset });
    }
}

// Карта указателей по всей памяти, сортировка по value.
std::vector<PointerRecord> buildPointerMap(const Process& process, const std::vector<Range>& regions,
                                           PointerScanStats& stats, const ProgressCallback& progress)
{
    std::vector<PointerRecord> map;
    std::vector<uint8_t> buffer(kChunkSize);
    auto lastReport = Clock::now();

    for (const Range& region : regions) {
        for (uintptr_t chunk = region.start; chunk < region.end; chunk += kChunkSize) {
            const size_t size = static_cast<size_t>(std::min<uintptr_t>(kChunkSize, region.end - chunk));
            if (!process.read(chunk, buffer.data(), size))
                continue; // не читается - пропускаем
            stats.bytesRead += size;

            if (process.pointerSize() == 8)
                collectPointers<uint64_t>(buffer.data(), size, chunk, regions, map);
            else
                collectPointers<uint32_t>(buffer.data(), size, chunk, regions, map);

            if (progress && Clock::now() - lastReport > std::chrono::milliseconds(300)) {
                lastReport = Clock::now();
                progress("Reading memory: " + std::to_string(stats.bytesRead / (1024 * 1024)) + " MB, "
                         + std::to_string(map.size()) + " pointers");
            }
        }
    }

    if (progress)
        progress("Sorting " + std::to_string(map.size()) + " pointers...");
    std::sort(map.begin(), map.end(),
              [](const PointerRecord& a, const PointerRecord& b) { return a.value < b.value; });
    return map;
}

struct Edge {
    uintptr_t parent;
    uintptr_t child;
    int64_t offset;
};

// Собирает готовые цепочки от статических адресов к цели.
class PathCollector {
public:
    PathCollector(const std::vector<Edge>& edgesByParent, const std::unordered_map<uintptr_t, int>& levels,
                  const std::vector<ModuleInfo>& modules, uintptr_t target, size_t maxResults,
                  std::vector<PointerPath>& out)
        : edges_(edgesByParent), levels_(levels), modules_(modules), target_(target),
          maxResults_(maxResults), out_(out)
    {
    }

    void collect(uintptr_t start, int length)
    {
        start_ = start;
        length_ = length;
        walk(start, 0);
    }

    bool full() const { return out_.size() >= maxResults_; }

private:
    void walk(uintptr_t node, int used)
    {
        auto range = std::equal_range(edges_.begin(), edges_.end(), Edge{ node, 0, 0 },
                                      [](const Edge& a, const Edge& b) { return a.parent < b.parent; });

        for (auto it = range.first; it != range.second && !full(); ++it) {
            const Edge& edge = *it;

            if (edge.child == target_) {
                if (used + 1 == length_) {
                    stack_.push_back(&edge);
                    emit();
                    stack_.pop_back();
                }
                continue;
            }

            auto level = levels_.find(edge.child);
            if (level == levels_.end() || used + 1 + level->second > length_)
                continue;
            if (onStack(edge.child))
                continue;

            stack_.push_back(&edge);
            walk(edge.child, used + 1);
            stack_.pop_back();
        }
    }

    bool onStack(uintptr_t address) const
    {
        if (address == start_)
            return true;
        for (const Edge* edge : stack_)
            if (edge->child == address)
                return true;
        return false;
    }

    void emit()
    {
        const ModuleInfo* module = moduleContaining(modules_, start_);
        if (!module)
            return;

        PointerPath path;
        path.module = module->name;
        path.moduleOffset = start_ - module->base;
        for (const Edge* edge : stack_)
            path.offsets.push_back(edge->offset);
        out_.push_back(std::move(path));
    }

    const std::vector<Edge>& edges_;
    const std::unordered_map<uintptr_t, int>& levels_;
    const std::vector<ModuleInfo>& modules_;
    uintptr_t target_;
    size_t maxResults_;
    std::vector<PointerPath>& out_;

    uintptr_t start_ = 0;
    int length_ = 0;
    std::vector<const Edge*> stack_;
};

} // namespace

PointerScanStats pointerScan(const Process& process, uintptr_t target, const PointerScanSettings& settings,
                             std::vector<PointerPath>& out, const ProgressCallback& progress)
{
    const auto start = Clock::now();
    PointerScanStats stats;
    out.clear();

    const std::vector<ModuleInfo> modules = process.modules();

    std::vector<Range> moduleRanges;
    for (const ModuleInfo& m : modules)
        moduleRanges.push_back({ m.base, m.base + m.size });
    std::sort(moduleRanges.begin(), moduleRanges.end(),
              [](const Range& a, const Range& b) { return a.start < b.start; });
    auto isStatic = [&](uintptr_t address) { return insideRanges(moduleRanges, address); };

    if (const ModuleInfo* module = moduleContaining(modules, target))
        out.push_back({ module->name, target - module->base, {} });

    const std::vector<Range> regions = collectRegions(process);
    if (regions.empty()) {
        stats.milliseconds = millisecondsSince(start);
        return stats;
    }

    // 1. Карта указателей.
    const std::vector<PointerRecord> map = buildPointerMap(process, regions, stats, progress);
    stats.pointers = map.size();

    // 2. Обратный поиск по уровням.
    std::unordered_map<uintptr_t, int> levels;
    std::vector<Edge> edges;
    std::vector<uintptr_t> frontier{ target };
    levels[target] = 0;

    const auto maxOffset = static_cast<uintptr_t>(settings.maxOffset);

    for (int level = 1; level <= settings.maxLevel && !frontier.empty(); level++) {
        if (progress)
            progress("Level " + std::to_string(level) + ": " + std::to_string(frontier.size()) + " addresses to check");

        std::vector<uintptr_t> next;
        for (const uintptr_t child : frontier) {
            auto it = std::upper_bound(map.begin(), map.end(), child,
                                       [](uintptr_t value, const PointerRecord& r) { return value < r.value; });
            int distinct = 0;
            uintptr_t lastValue = 0;

            while (it != map.begin()) {
                --it;
                if (child - it->value > maxOffset)
                    break;
                if (distinct == 0 || it->value != lastValue) {
                    if (++distinct > settings.maxOffsetsPerNode)
                        break;
                    lastValue = it->value;
                }

                edges.push_back({ it->address, child, static_cast<int64_t>(child - it->value) });

                if (levels.emplace(it->address, level).second && !isStatic(it->address))
                    next.push_back(it->address);
            }

            if (edges.size() >= kMaxEdges || levels.size() >= kMaxNodes) {
                stats.limited = true;
                break;
            }
        }

        stats.levelsDone = level;
        if (stats.limited)
            break;
        frontier.swap(next);
    }
    stats.nodes = levels.size();

    // 3. Собираем цепочки, короткие первыми.
    if (progress)
        progress("Collecting paths...");

    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) { return a.parent < b.parent; });

    std::vector<std::pair<int, uintptr_t>> statics;
    for (const auto& entry : levels)
        if (entry.second > 0 && isStatic(entry.first))
            statics.push_back({ entry.second, entry.first });
    std::sort(statics.begin(), statics.end());

    PathCollector collector(edges, levels, modules, target, settings.maxResults, out);
    for (int length = 1; length <= stats.levelsDone && !collector.full(); length++)
        for (const auto& s : statics)
            if (s.first <= length && !collector.full())
                collector.collect(s.second, length);

    stats.milliseconds = millisecondsSince(start);
    return stats;
}
