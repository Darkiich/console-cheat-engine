#include "core/Process.h"

#include <tlhelp32.h>

std::vector<ProcessInfo> Process::list()
{
    std::vector<ProcessInfo> result;

    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid())
        return result;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot.get(), &entry)) {
        do {
            result.push_back({ entry.th32ProcessID, entry.szExeFile });
        } while (Process32NextW(snapshot.get(), &entry));
    }
    return result;
}

bool Process::open(DWORD processId)
{
    handle_.reset(OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId));
    pid_ = handle_.valid() ? processId : 0;
    return handle_.valid();
}

bool Process::isAlive() const
{
    // Спрашиваю текущее состояние.
    return handle_.valid() && WaitForSingleObject(handle_.get(), 0) == WAIT_TIMEOUT;
}

bool Process::read(uintptr_t address, void* buffer, size_t size) const
{
    SIZE_T done = 0;
    return ReadProcessMemory(handle_.get(), reinterpret_cast<LPCVOID>(address), buffer, size, &done)
        && done == size;
}

bool Process::write(uintptr_t address, const void* data, size_t size) const
{
    SIZE_T done = 0;
    return WriteProcessMemory(handle_.get(), reinterpret_cast<LPVOID>(address), data, size, &done)
        && done == size;
}

bool Process::readValue(uintptr_t address, ValueType type, Value& out) const
{
    out = Value{};
    return read(address, out.bytes, valueSize(type));
}

bool Process::writeValue(uintptr_t address, ValueType type, const Value& value) const
{
    return write(address, value.bytes, valueSize(type));
}
