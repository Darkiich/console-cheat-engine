#include <cstdio>

#include "core/Process.h"
#include "ui/Input.h"
#include "ui/Menu.h"

static const char* kSaveFile = "adres.txt";

int main()
{
    try {
        const std::vector<ProcessInfo> processes = Process::list();
        if (processes.empty()) {
            std::puts("Failed to get process list");
            return 1;
        }
        for (const ProcessInfo& p : processes)
            std::printf("PID: %5lu | Name: %ls\n", p.pid, p.name.c_str());

        // Если процесс не открылся - спрашиваем снова, а не выходим из программы.
        Process process;
        for (;;) {
            const auto pid = static_cast<DWORD>(askNumber("Enter PID (0 - exit): ", 0, 0xFFFFFFFFLL));
            if (pid == 0)
                return 0;
            if (process.open(pid))
                break;
            std::printf("Failed to open process %lu (error %lu). Try another PID.\n", pid, GetLastError());
        }

        SYSTEM_INFO info;
        GetSystemInfo(&info);
        std::printf("Opened PID %lu. Scan range: 0x%012llX - 0x%012llX\n",
                    process.pid(),
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress)),
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress)));

        runMenu(process, kSaveFile);
    }
    catch (const InputClosed&) {
        // Закрываем ввод. Деструкторы всё закроют и остановят поток заморозки.
    }
    return 0;
}
