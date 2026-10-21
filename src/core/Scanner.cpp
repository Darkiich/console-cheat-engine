#include "core/Scanner.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

// Снимок памяти для неизвестного значения
class MemorySnapshot {
public:
    struct Region {
        uintptr_t base;
        size_t size;
        uint64_t fileOffset;
        std::vector<bool> alive;
    };

    ~MemorySnapshot()
    {
        data.close();
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
    }

    std::filesystem::path file;
    std::fstream data;
    std::vector<Region> regions;
    size_t count = 0;
};

size_t ScanResults::count() const
{
    return snapshot ? snapshot->count : hits.size();
}

namespace {

// Читаем регион кусками по 4 МБ в тот же буфер.
constexpr size_t kChunkSize = 4 * 1024 * 1024;

constexpr size_t kMaxGap = 4096;
constexpr size_t kMaxSpan = 1024 * 1024;

constexpr size_t kMaxListSize = 5'000'000;

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

size_t pageSize()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
}

// Сканируем регионы - выделенные, без PAGE_GUARD/PAGE_NOACCESS и доступные на запись.
bool isScannable(const MEMORY_BASIC_INFORMATION& mbi)
{
    if (mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & writable) != 0;
}

// Обходим все регионы для скана.
template <typename F>
void forEachRegion(const Process& process, F&& visit)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);

    uintptr_t address = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t maxAddress = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    MEMORY_BASIC_INFORMATION mbi;

    while (address < maxAddress
           && VirtualQueryEx(process.handle(), reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize == 0)
            break;

        if (isScannable(mbi))
            visit(base, static_cast<size_t>(mbi.RegionSize));

        address = base + mbi.RegionSize; // переход на некст регион
    }
}

// Ищет то, что надо в уже прочитанном буфере.
// Шаг = sizeof(T)
template <typename T>
void scanBuffer(const uint8_t* data, size_t size, uintptr_t base, const SearchValue& target, std::vector<ScanHit>& out)
{
    for (size_t offset = 0; offset + sizeof(T) <= size; offset += sizeof(T)) {
        T value;
        std::memcpy(&value, data + offset, sizeof(T));
        if (searchMatches(value, target))
            out.push_back({ base + offset, Value::from(value) });
    }
}

// Читает и сканирует один кусок региона.
template <typename T>
void scanChunk(const Process& process, uintptr_t address, size_t size, size_t page, const SearchValue& target,
               std::vector<uint8_t>& buffer, std::vector<ScanHit>& out, ScanStats& stats)
{
    stats.readCalls++;
    if (process.read(address, buffer.data(), size)) {
        stats.bytes += size;
        scanBuffer<T>(buffer.data(), size, address, target, out);
        return;
    }

    for (size_t offset = 0; offset < size; offset += page) {
        const size_t length = std::min(page, size - offset);
        stats.readCalls++;
        if (process.read(address + offset, buffer.data(), length)) {
            stats.bytes += length;
            scanBuffer<T>(buffer.data(), length, address + offset, target, out);
        }
    }
}

template <typename T>
void firstScanTyped(const Process& process, const SearchValue& target, std::vector<ScanHit>& out, ScanStats& stats)
{
    const size_t page = pageSize();
    std::vector<uint8_t> buffer(kChunkSize);

    forEachRegion(process, [&](uintptr_t base, size_t size) {
        stats.regions++;
        for (size_t offset = 0; offset < size; offset += kChunkSize) {
            const size_t length = std::min(kChunkSize, size - offset);
            scanChunk<T>(process, base + offset, length, page, target, buffer, out, stats);
        }
    });
}

template <typename T>
bool matches(NextScanKind kind, T current, T previous, const SearchValue& compare)
{
    switch (kind) {
    case NextScanKind::Increased: return current > previous;
    case NextScanKind::Decreased: return current < previous;
    case NextScanKind::Exact: return searchMatches(current, compare);
    case NextScanKind::Changed: return !valuesEqual(current, previous);
    case NextScanKind::Unchanged: return valuesEqual(current, previous);
    }
    return false;
}

template <typename T>
void nextScanTyped(const Process& process, NextScanKind kind, const SearchValue& compare, std::vector<ScanHit>& hits,
                   ScanStats& stats)
{
    std::vector<uint8_t> buffer;

    size_t kept = 0;

    size_t first = 0;
    while (first < hits.size()) {
        // 1. Собираем группу адресов.
        const uintptr_t spanStart = hits[first].address;
        uintptr_t spanEnd = spanStart + sizeof(T);
        size_t last = first + 1;
        while (last < hits.size()
               && hits[last].address <= spanEnd + kMaxGap
               && hits[last].address + sizeof(T) - spanStart <= kMaxSpan)
        {
            spanEnd = std::max(spanEnd, hits[last].address + sizeof(T));
            last++;
        }

        // 2. Читаем всю группу разом.
        const size_t spanSize = spanEnd - spanStart;
        if (buffer.size() < spanSize)
            buffer.resize(spanSize);

        stats.readCalls++;
        const bool spanRead = process.read(spanStart, buffer.data(), spanSize);
        if (spanRead)
            stats.bytes += spanSize;

        // 3. Проверяем каждый адрес группы.
        for (size_t i = first; i < last; i++) {
            T current;
            if (spanRead) {
                std::memcpy(&current, buffer.data() + (hits[i].address - spanStart), sizeof(T));
            } else {
                stats.readCalls++;
                if (!process.read(hits[i].address, &current, sizeof(T)))
                    continue;
                stats.bytes += sizeof(T);
            }

            if (matches(kind, current, hits[i].previous.as<T>(), compare)) {
                hits[kept].address = hits[i].address;
                hits[kept].previous = Value::from(current);
                kept++;
            }
        }

        first = last;
    }

    hits.resize(kept);
    hits.shrink_to_fit();  // верём системе память, если результатов стало меньше
}

void readChunk(const Process& process, uintptr_t address, size_t size, size_t page, uint8_t* buffer,
               std::vector<std::pair<size_t, size_t>>& bad, ScanStats& stats)
{
    bad.clear();
    stats.readCalls++;
    if (process.read(address, buffer, size)) {
        stats.bytes += size;
        return;
    }

    for (size_t offset = 0; offset < size; offset += page) {
        const size_t length = std::min(page, size - offset);
        stats.readCalls++;
        if (process.read(address + offset, buffer + offset, length)) {
            stats.bytes += length;
        } else {
            std::memset(buffer + offset, 0, length);
            bad.push_back({ offset, length });
        }
    }
}

// Всю память в файл.
bool takeSnapshot(const Process& process, size_t valueBytes, MemorySnapshot& snapshot, ScanStats& stats)
{
    const size_t page = pageSize();
    std::vector<uint8_t> buffer(kChunkSize);
    std::vector<std::pair<size_t, size_t>> bad;
    uint64_t fileOffset = 0;

    forEachRegion(process, [&](uintptr_t base, size_t size) {
        stats.regions++;
        MemorySnapshot::Region region{ base, size, fileOffset, std::vector<bool>(size / valueBytes, true) };

        for (size_t offset = 0; offset < size; offset += kChunkSize) {
            const size_t length = std::min(kChunkSize, size - offset);
            readChunk(process, base + offset, length, page, buffer.data(), bad, stats);
            for (const auto& b : bad)
                std::fill_n(region.alive.begin() + static_cast<std::ptrdiff_t>((offset + b.first) / valueBytes),
                            b.second / valueBytes, false);
            snapshot.data.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(length));
        }

        fileOffset += size;
        snapshot.count += static_cast<size_t>(std::count(region.alive.begin(), region.alive.end(), true));
        snapshot.regions.push_back(std::move(region));
    });

    return static_cast<bool>(snapshot.data);
}

template <typename T>
void nextScanSnapshot(const Process& process, NextScanKind kind, const SearchValue& compare,
                      MemorySnapshot& snapshot, ScanStats& stats)
{
    const size_t page = pageSize();
    std::vector<uint8_t> current(kChunkSize);
    std::vector<uint8_t> previous(kChunkSize);
    std::vector<std::pair<size_t, size_t>> bad;
    size_t count = 0;

    for (MemorySnapshot::Region& region : snapshot.regions) {
        for (size_t offset = 0; offset < region.size; offset += kChunkSize) {
            const size_t length = std::min(kChunkSize, region.size - offset);
            const auto position = static_cast<std::streamoff>(region.fileOffset + offset);

            snapshot.data.seekg(position);
            snapshot.data.read(reinterpret_cast<char*>(previous.data()), static_cast<std::streamsize>(length));
            readChunk(process, region.base + offset, length, page, current.data(), bad, stats);
            for (const auto& b : bad)
                std::fill_n(region.alive.begin() + static_cast<std::ptrdiff_t>((offset + b.first) / sizeof(T)),
                            b.second / sizeof(T), false);

            const size_t firstSlot = offset / sizeof(T);
            for (size_t i = 0; i < length / sizeof(T); i++) {
                if (!region.alive[firstSlot + i])
                    continue;
                T now;
                T before;
                std::memcpy(&now, current.data() + i * sizeof(T), sizeof(T));
                std::memcpy(&before, previous.data() + i * sizeof(T), sizeof(T));
                if (matches(kind, now, before, compare))
                    count++;
                else
                    region.alive[firstSlot + i] = false;
            }

            snapshot.data.seekp(position);
            snapshot.data.write(reinterpret_cast<const char*>(current.data()), static_cast<std::streamsize>(length));
        }
    }
    snapshot.count = count;
}

// Сбор оставщихся в список.
template <typename T>
void snapshotToList(MemorySnapshot& snapshot, std::vector<ScanHit>& hits)
{
    std::vector<uint8_t> buffer(kChunkSize);
    hits.clear();
    hits.reserve(snapshot.count);

    for (const MemorySnapshot::Region& region : snapshot.regions) {
        for (size_t offset = 0; offset < region.size; offset += kChunkSize) {
            const size_t length = std::min(kChunkSize, region.size - offset);
            snapshot.data.seekg(static_cast<std::streamoff>(region.fileOffset + offset));
            snapshot.data.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(length));

            const size_t firstSlot = offset / sizeof(T);
            for (size_t i = 0; i < length / sizeof(T); i++) {
                if (!region.alive[firstSlot + i])
                    continue;
                T value;
                std::memcpy(&value, buffer.data() + i * sizeof(T), sizeof(T));
                hits.push_back({ region.base + offset + i * sizeof(T), Value::from(value) });
            }
        }
    }
}

template <typename T>
void nextScanAny(const Process& process, NextScanKind kind, const SearchValue& compare, ScanResults& results,
                 ScanStats& stats)
{
    if (!results.snapshot) {
        nextScanTyped<T>(process, kind, compare, results.hits, stats);
        return;
    }

    MemorySnapshot& snapshot = *results.snapshot;
    nextScanSnapshot<T>(process, kind, compare, snapshot, stats);
    if (!snapshot.data) {
        stats.error = "failed to read/write the snapshot file";
        return;
    }
    if (snapshot.count <= kMaxListSize) {
        snapshotToList<T>(snapshot, results.hits);
        results.snapshot.reset();
    }
}

} // namespace

ScanStats firstScan(const Process& process, ValueType type, const SearchValue& target, ScanResults& out)
{
    const auto start = Clock::now();
    ScanStats stats;

    out.type = type;
    out.hits = std::vector<ScanHit>();
    out.snapshot.reset();

    switch (type) {
    case ValueType::Short: firstScanTyped<int16_t>(process, target, out.hits, stats); break;
    case ValueType::Int: firstScanTyped<int32_t>(process, target, out.hits, stats); break;
    case ValueType::Float: firstScanTyped<float>(process, target, out.hits, stats); break;
    case ValueType::Double: firstScanTyped<double>(process, target, out.hits, stats); break;
    case ValueType::Byte: firstScanTyped<uint8_t>(process, target, out.hits, stats); break;
    case ValueType::Int64: firstScanTyped<int64_t>(process, target, out.hits, stats); break;
    }

    stats.milliseconds = millisecondsSince(start);
    return stats;
}

ScanStats firstScanUnknown(const Process& process, ValueType type, ScanResults& out)
{
    const auto start = Clock::now();
    ScanStats stats;

    out.type = type;
    out.hits = std::vector<ScanHit>();
    out.snapshot.reset();

    static int counter = 0;
    std::error_code ec;
    auto snapshot = std::make_shared<MemorySnapshot>();
    snapshot->file = std::filesystem::temp_directory_path(ec)
        / ("cce_snapshot_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(++counter) + ".bin");
    snapshot->data.open(snapshot->file, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    if (!snapshot->data) {
        stats.error = "can't create " + snapshot->file.u8string();
        return stats;
    }

    if (!takeSnapshot(process, valueSize(type), *snapshot, stats)) {
        stats.error = "can't write " + snapshot->file.u8string() + " (not enough disk space?)";
        return stats;
    }

    out.snapshot = std::move(snapshot);
    stats.milliseconds = millisecondsSince(start);
    return stats;
}

ScanStats nextScan(const Process& process, NextScanKind kind, const SearchValue& compare, ScanResults& results)
{
    const auto start = Clock::now();
    ScanStats stats;

    switch (results.type) {
    case ValueType::Short: nextScanAny<int16_t>(process, kind, compare, results, stats); break;
    case ValueType::Int: nextScanAny<int32_t>(process, kind, compare, results, stats); break;
    case ValueType::Float: nextScanAny<float>(process, kind, compare, results, stats); break;
    case ValueType::Double: nextScanAny<double>(process, kind, compare, results, stats); break;
    case ValueType::Byte: nextScanAny<uint8_t>(process, kind, compare, results, stats); break;
    case ValueType::Int64: nextScanAny<int64_t>(process, kind, compare, results, stats); break;
    }

    stats.milliseconds = millisecondsSince(start);
    return stats;
}
