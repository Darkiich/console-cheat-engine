#include "core/Scanner.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace {

// Читаем регион кусками по 4 МБ в тот же буфер.
constexpr size_t kChunkSize = 4 * 1024 * 1024;

constexpr size_t kMaxGap = 4096;
constexpr size_t kMaxSpan = 1024 * 1024;

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Какие регионы сканируем - выделенные, без PAGE_GUARD/PAGE_NOACCESS и доступные на запись.
bool isScannable(const MEMORY_BASIC_INFORMATION& mbi)
{
    if (mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & writable) != 0;
}

// Ищет то, что надо в уже прочитанном буфере.
// Шаг = sizeof(T)
template <typename T>
void scanBuffer(const uint8_t* data, size_t size, uintptr_t base, T target, std::vector<ScanHit>& out)
{
    for (size_t offset = 0; offset + sizeof(T) <= size; offset += sizeof(T)) {
        T value;
        std::memcpy(&value, data + offset, sizeof(T));
        if (valuesEqual(value, target))
            out.push_back({ base + offset, Value::from(value) }); // тут будет реальное значение
    }
}

// Читает и сканирует один кусок региона.
template <typename T>
void scanChunk(const Process& process, uintptr_t address, size_t size, size_t pageSize, T target,
               std::vector<uint8_t>& buffer, std::vector<ScanHit>& out, ScanStats& stats)
{
    stats.readCalls++;
    if (process.read(address, buffer.data(), size)) {
        stats.bytes += size;
        scanBuffer(buffer.data(), size, address, target, out);
        return;
    }

    for (size_t offset = 0; offset < size; offset += pageSize) {
        const size_t length = std::min(pageSize, size - offset);
        stats.readCalls++;
        if (process.read(address + offset, buffer.data(), length)) {
            stats.bytes += length;
            scanBuffer(buffer.data(), length, address + offset, target, out);
        }
    }
}

template <typename T>
void firstScanTyped(const Process& process, T target, std::vector<ScanHit>& out, ScanStats& stats)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);

    uintptr_t address = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t maxAddress = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);

    std::vector<uint8_t> buffer(kChunkSize);
    MEMORY_BASIC_INFORMATION mbi;

    while (address < maxAddress
           && VirtualQueryEx(process.handle(), reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize == 0)
            break;

        if (isScannable(mbi)) {
            stats.regions++;
            for (size_t offset = 0; offset < mbi.RegionSize; offset += kChunkSize) {
                const size_t length = std::min(kChunkSize, static_cast<size_t>(mbi.RegionSize) - offset);
                scanChunk(process, base + offset, length, info.dwPageSize, target, buffer, out, stats);
            }
        }

        address = base + mbi.RegionSize; // переход на некст регион
    }
}

template <typename T>
bool matches(NextScanKind kind, T current, T previous, T compare)
{
    switch (kind) {
    case NextScanKind::Increased: return current > previous;
    case NextScanKind::Decreased: return current < previous;
    case NextScanKind::Exact: return valuesEqual(current, compare);
    case NextScanKind::Changed: return !valuesEqual(current, previous);
    case NextScanKind::Unchanged: return valuesEqual(current, previous);
    }
    return false;
}

template <typename T>
void nextScanTyped(const Process& process, NextScanKind kind, T compare, std::vector<ScanHit>& hits, ScanStats& stats)
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
                // Читаем адреса по одному.
                stats.readCalls++;
                if (!process.read(hits[i].address, &current, sizeof(T)))
                    continue;  // Выкидываем его из результатов
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

} // namespace

ScanStats firstScan(const Process& process, ValueType type, const Value& target, ScanResults& out)
{
    const auto start = Clock::now();
    ScanStats stats;

    out.type = type;
    out.hits = std::vector<ScanHit>();

    switch (type) {
    case ValueType::Short: firstScanTyped(process, target.as<int16_t>(), out.hits, stats); break;
    case ValueType::Int: firstScanTyped(process, target.as<int32_t>(), out.hits, stats); break;
    case ValueType::Float: firstScanTyped(process, target.as<float>(), out.hits, stats); break;
    case ValueType::Double: firstScanTyped(process, target.as<double>(), out.hits, stats); break;
    }

    stats.milliseconds = millisecondsSince(start);
    return stats;
}

ScanStats nextScan(const Process& process, NextScanKind kind, const Value& compare, ScanResults& results)
{
    const auto start = Clock::now();
    ScanStats stats;

    switch (results.type) {
    case ValueType::Short: nextScanTyped(process, kind, compare.as<int16_t>(), results.hits, stats); break;
    case ValueType::Int: nextScanTyped(process, kind, compare.as<int32_t>(), results.hits, stats); break;
    case ValueType::Float: nextScanTyped(process, kind, compare.as<float>(), results.hits, stats); break;
    case ValueType::Double: nextScanTyped(process, kind, compare.as<double>(), results.hits, stats); break;
    }

    stats.milliseconds = millisecondsSince(start);
    return stats;
}
