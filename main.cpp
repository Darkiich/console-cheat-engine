#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif // !_CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <tlhelp32.h>

#include "structs.h"
#include "actionswithmemory.h"

using namespace std;

static const char* SAVE_FILE = "adres.txt";

int main()
{
    DWORD pidProc; // Переменная для записи pid процесса

    HANDLE pHandle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); // Дескриптор и снапшот для всех процессов
    if (pHandle == INVALID_HANDLE_VALUE)
    {
        puts("Failed to get process list");
        return 1;
    }

    PROCESSENTRY32 pe32; // Структура для хранения данных о процессе
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(pHandle, &pe32)) {
        puts("Failed to get first process");
        CloseHandle(pHandle);
        return 1;
    }

    while (Process32Next(pHandle, &pe32)) { printf("PID: %5lu | Name: %ls\n", pe32.th32ProcessID, pe32.szExeFile); } // Передаю данные каждого процесса в структуру
    CloseHandle(pHandle);

    printf("Enter PID: ");
    if (scanf_s("%lu", &pidProc) != 1) return 1;

    HANDLE procHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pidProc); // Дескриптор для открытого процесса
    if (procHandle == NULL)
    {
        printf("Failed to open process %lu", pidProc);
        return 1;
    }

    SYSTEM_INFO sInfo; // Структура, для получения минимального и максимального значения виртуальной памяти
    GetSystemInfo(&sInfo);

    int choice = 0; // Выбор действия

    ScanContext sContext;
    sContext.procHandle = procHandle;
    sContext.sInfo = sInfo;
    sContext.foundAddresses = {};
    sContext.choiceType = 0;
    sContext.foundType = sizeof(int);

    printf("Scan range: from 0x%p to 0x%p\n", sContext.sInfo.lpMinimumApplicationAddress, sContext.sInfo.lpMaximumApplicationAddress);

    while (true)
    {
        printf("\n1. First Scan\n");
        printf("2. Next Scan\n");
        printf("3. Change value\n");
        printf("4. Freeze value\n");
        printf("5. Unfreeze value\n");
        printf("6. Show found addresses\n");
        printf("7. Save addresses to file\n");
        printf("8. Load addresses from file\n");
        printf("0. Exit\n");
        printf("Choice: ");
        scanf_s("%d", &choice);

        switch (choice)
        {
            case 0: CloseHandle(procHandle); return 0;
            case 1: actionFirstScan(sContext); break;
            case 2: actionNextScan(sContext); break;
            case 3: actionChangeValue(sContext); break;
            case 4: actionFreezeValue(sContext); break;
            case 5: actionUnfreezeValue(sContext); break;
            case 6: actionShowFoundedAddresses(sContext); break;
            case 7: actionSaveFoundedAddresses(sContext, SAVE_FILE); break;
            case 8: actionLoadFoundedAddresses(sContext, SAVE_FILE); break;
            default: puts("Input number isn't range between 1 and 8"); break;
        }
    }
    CloseHandle(procHandle);
    return 0;
}