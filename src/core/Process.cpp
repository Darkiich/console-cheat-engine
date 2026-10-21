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

    // 32-битная игра - указатели по 4 байта
    BOOL isWow64 = FALSE;
    pointerSize_ = (handle_.valid() && IsWow64Process(handle_.get(), &isWow64) && isWow64) ? 4 : 8;

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

bool Process::readPointer(uintptr_t address, uintptr_t& out) const
{
    out = 0;
    return read(address, &out, pointerSize_);
}

std::string Process::exeName() const
{
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    if (!QueryFullProcessImageNameW(handle_.get(), 0, path, &size))
        return "";

    const std::wstring full(path, size);
    const size_t slash = full.find_last_of(L"\\/");
    return toUtf8(slash == std::wstring::npos ? full : full.substr(slash + 1));
}

std::vector<ModuleInfo> Process::modules() const
{
    std::vector<ModuleInfo> result;

    // снапшот иногда не создаётся с первого раза
    UniqueHandle snapshot;
    for (int attempt = 0; attempt < 5 && !snapshot.valid(); attempt++)
        snapshot.reset(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_));
    if (!snapshot.valid())
        return result;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot.get(), &entry)) {
        do {
            result.push_back({ toUtf8(entry.szModule),
                               reinterpret_cast<uintptr_t>(entry.modBaseAddr),
                               static_cast<size_t>(entry.modBaseSize) });
        } while (Module32NextW(snapshot.get(), &entry));
    }
    return result;
}

std::string toUtf8(const std::wstring& text)
{
    if (text.empty())
        return "";
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        &result[0], length, nullptr, nullptr);
    return result;
}
