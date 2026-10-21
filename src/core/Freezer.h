#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "core/Pointer.h"
#include "core/Process.h"
#include "core/Value.h"

struct FrozenEntry {
    uintptr_t address;
    ValueType type;
    Value value;

    // Для значений из таблицы адрес считается по цепочке на каждом тике.
    std::optional<PointerPath> path;
    uintptr_t pathStart = 0; // база модуля + смещение
};

class Freezer {
public:
    explicit Freezer(const Process& process);
    ~Freezer();  // останавливает поток и дожидается его завершения

    Freezer(const Freezer&) = delete;
    Freezer& operator=(const Freezer&) = delete;

    bool add(const FrozenEntry& entry); // false - адрес уже заморожен
    bool remove(uintptr_t address); // false - адрес не был заморожен
    bool updateValue(uintptr_t address, const Value& value); // false - адрес не заморожен
    bool contains(uintptr_t address) const;
    std::vector<FrozenEntry> entries() const; // копия списка, чтобы вывывести на экран

    // То же самое, но для таблицы.
    bool remove(const PointerPath& path);
    bool updateValue(const PointerPath& path, const Value& value);
    bool contains(const PointerPath& path) const;

private:
    void run(); // фоновый поток

    static constexpr std::chrono::milliseconds kInterval{ 50 };

    const Process& process_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<FrozenEntry> entries_;
    bool stopping_ = false;
    std::thread thread_;
};
