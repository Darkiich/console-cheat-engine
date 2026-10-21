#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/Process.h"
#include "core/Table.h"
#include "ui/Input.h"
#include "ui/Menu.h"

static const char* kSaveFile = "adres.txt";

namespace {

std::string toLower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Ищем процесс по имени, 0 - не нашли.
DWORD findProcessByName(const std::vector<ProcessInfo>& processes, const std::string& name)
{
    const std::string wanted = toLower(name);
    for (const ProcessInfo& p : processes)
        if (toLower(toUtf8(p.name)) == wanted)
            return p.pid;
    return 0;
}

// Выбор процесса.
bool chooseProcess(Process& process)
{
    const std::vector<ProcessInfo> processes = Process::list();
    if (processes.empty()) {
        std::puts("Failed to get process list");
        return false;
    }

    // Сначала игры, для которых уже есть таблица.
    std::vector<const ProcessInfo*> games;
    for (const ProcessInfo& p : processes)
        if (tableExists(toUtf8(p.name)))
            games.push_back(&p);

    if (!games.empty()) {
        std::puts("Running games with saved tables:");
        for (size_t i = 0; i < games.size(); i++)
            std::printf("%4zu. %s (PID %lu)\n", i + 1, toUtf8(games[i]->name).c_str(), games[i]->pid);
        std::puts("   0. Choose another process");

        const long long choice = askNumber("Choice: ", 0, static_cast<long long>(games.size()));
        if (choice > 0) {
            const ProcessInfo& game = *games[static_cast<size_t>(choice - 1)];
            if (process.open(game.pid))
                return true;
            std::printf("Failed to open %s (error %lu)\n", toUtf8(game.name).c_str(), GetLastError());
        }
    }

    for (const ProcessInfo& p : processes)
        std::printf("PID: %5lu | Name: %ls\n", p.pid, p.name.c_str());

    // Если процесс не открылся - спрашиваем снова, а не выходим из программы.
    for (;;) {
        const std::string answer = askLine("Enter PID or process name (0 - exit): ");
        if (answer.empty())
            continue;
        if (answer == "0")
            return false;

        // число - PID, иначе имя процесса
        char* end = nullptr;
        const unsigned long number = std::strtoul(answer.c_str(), &end, 10);
        const DWORD pid = (*end == '\0') ? static_cast<DWORD>(number)
                                         : findProcessByName(Process::list(), answer);
        if (pid == 0) {
            std::printf("Process '%s' not found\n", answer.c_str());
            continue;
        }
        if (process.open(pid))
            return true;
        std::printf("Failed to open process %lu (error %lu). Try another PID.\n", pid, GetLastError());
    }
}

// Ждём, пока игру запустят снова.
bool waitForRestart(Process& process, const std::string& game)
{
    std::printf("Waiting for %s to start again... (Ctrl+C to quit)\n", game.c_str());
    for (;;) {
        const DWORD pid = findProcessByName(Process::list(), game);
        if (pid != 0) {
            Sleep(1000); // даём игре прогрузиться
            if (process.open(pid))
                return true;
        }
        Sleep(1000);
    }
}

} // namespace

int main()
{
    try {
        Process process;
        if (!chooseProcess(process))
            return 0;

        for (;;) {
            const std::string game = process.exeName();

            SYSTEM_INFO info;
            GetSystemInfo(&info);
            std::printf("Opened %s (PID %lu, %zu-bit). Scan range: 0x%012llX - 0x%012llX\n",
                        game.c_str(), process.pid(), process.pointerSize() * 8,
                        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress)),
                        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress)));

            if (runMenu(process, kSaveFile) == MenuExit::User)
                return 0;

            // Игра закрылась, можно дождаться перезапуска.
            const std::string answer = askLine(("Wait for " + game + " to restart? (y/n): ").c_str());
            if (answer != "y" && answer != "Y")
                return 0;
            if (!waitForRestart(process, game))
                return 0;
        }
    }
    catch (const InputClosed&) {
        // Закрываем ввод. Деструкторы всё закроют и остановят поток заморозки.
    }
    return 0;
}
