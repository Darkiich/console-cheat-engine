#include "core/Freezer.h"

#include <algorithm>

Freezer::Freezer(const Process& process)
    : process_(process)
{
    thread_ = std::thread(&Freezer::run, this);
}

Freezer::~Freezer()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
    thread_.join();
}

bool Freezer::add(const FrozenEntry& entry)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const FrozenEntry& existing : entries_) {
            // из таблицы сравниваем по цепочке, обычное по адресу
            const bool same = entry.path ? (existing.path && *existing.path == *entry.path)
                                         : (!existing.path && existing.address == entry.address);
            if (same)
                return false;
        }
        entries_.push_back(entry);
    }
    wake_.notify_one();  // будим, если спит
    return true;
}

bool Freezer::remove(uintptr_t address)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [address](const FrozenEntry& e) { return e.address == address; });
    if (it == entries_.end())
        return false;
    entries_.erase(it);
    return true;
}

bool Freezer::updateValue(uintptr_t address, const Value& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (FrozenEntry& entry : entries_) {
        if (entry.address == address) {
            entry.value = value;
            return true;
        }
    }
    return false;
}

bool Freezer::contains(uintptr_t address) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const FrozenEntry& entry : entries_)
        if (entry.address == address)
            return true;
    return false;
}

bool Freezer::remove(const PointerPath& path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&path](const FrozenEntry& e) { return e.path && *e.path == path; });
    if (it == entries_.end())
        return false;
    entries_.erase(it);
    return true;
}

bool Freezer::updateValue(const PointerPath& path, const Value& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (FrozenEntry& entry : entries_) {
        if (entry.path && *entry.path == path) {
            entry.value = value;
            return true;
        }
    }
    return false;
}

bool Freezer::contains(const PointerPath& path) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const FrozenEntry& entry : entries_)
        if (entry.path && *entry.path == path)
            return true;
    return false;
}

std::vector<FrozenEntry> Freezer::entries() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
}

void Freezer::run()
{
    std::vector<FrozenEntry> snapshot;
    std::unique_lock<std::mutex> lock(mutex_);

    while (!stopping_) {
        if (entries_.empty()) {
            wake_.wait(lock, [this] { return stopping_ || !entries_.empty(); });
            continue;
        }

        snapshot = entries_;
        lock.unlock();
        for (const FrozenEntry& entry : snapshot) {
            uintptr_t address = entry.address;
            if (entry.path) {
                // адрес каждый раз по цепочке, не нашли - пропускаем
                const std::optional<uintptr_t> resolved = followOffsets(process_, entry.pathStart, entry.path->offsets);
                if (!resolved)
                    continue;
                address = *resolved;
            }
            process_.writeValue(address, entry.type, entry.value);
        }
        lock.lock();

        wake_.wait_for(lock, kInterval, [this] { return stopping_; });
    }
}
