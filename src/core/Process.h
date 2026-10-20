#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Value.h"
#include "core/Win.h"

// Структура для хранения каждого PID и имени процесса.
struct ProcessInfo {
    DWORD pid;
    std::wstring name;
};

// Все чтения/записи памяти чужого процесса идут через этот класс.
class Process {
public:
    // Снапшот всех процессов в системе.
    static std::vector<ProcessInfo> list();

    bool open(DWORD processId);
    bool isOpen() const { return handle_.valid(); }
    bool isAlive() const;
    DWORD pid() const { return pid_; }
    HANDLE handle() const { return handle_.get(); }

	// Чтение/запись региона памяти.
    bool read(uintptr_t address, void* buffer, size_t size) const;
    bool write(uintptr_t address, const void* data, size_t size) const;

    // Чтение/запись значения выбранного типа.
    bool readValue(uintptr_t address, ValueType type, Value& out) const;
    bool writeValue(uintptr_t address, ValueType type, const Value& value) const;

private:
    UniqueHandle handle_;
    DWORD pid_ = 0;
};
