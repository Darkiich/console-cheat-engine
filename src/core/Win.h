#pragma once

// Единая точка подключения. Везде включаем этот файл, а не <windows.h> напрямую.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// RAII-обёртка.
// Копировать запрещено, можно передавать владение дескриптором.
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    // Получаю текущий дескриптор
    HANDLE get() const { return handle_; }

    // Проверка на затёртость и валидный дескриптор
    bool valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    // Закрываю текущий дескриптор
    void reset(HANDLE handle = nullptr)
    {
        if (valid())
            CloseHandle(handle_);
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};
