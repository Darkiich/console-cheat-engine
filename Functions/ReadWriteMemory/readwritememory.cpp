#include "readwritememory.h"

double readValue(HANDLE proc, ScanEntry& entry)
{
    switch (entry.typeValue) {
    case 1:
        if (ReadProcessMemory(proc, entry.address, &entry.tv.s, entry.byteValue, nullptr))
            return entry.tv.s;
        break;
    case 2:
        if (ReadProcessMemory(proc, entry.address, &entry.tv.i, entry.byteValue, nullptr))
            return entry.tv.i;
        break;
    case 3:
        if (ReadProcessMemory(proc, entry.address, &entry.tv.f, entry.byteValue, nullptr))
            return entry.tv.f;
        break;
    case 4:
        if (ReadProcessMemory(proc, entry.address, &entry.tv.d, entry.byteValue, nullptr))
            return entry.tv.d;
        break;
    }
    return 0.0;
}

bool writeValue(HANDLE proc, ScanEntry& entry, double newValue)
{
    switch (entry.typeValue) {
    case 1: {
        short val = (short)newValue;
        return WriteProcessMemory(proc, entry.address, &val, entry.byteValue, nullptr);
    }
    case 2: {
        int val = (int)newValue;
        return WriteProcessMemory(proc, entry.address, &val, entry.byteValue, nullptr);
    }
    case 3: {
        float val = (float)newValue;
        return WriteProcessMemory(proc, entry.address, &val, entry.byteValue, nullptr);
    }
    case 4: {
        double val = newValue;
        return WriteProcessMemory(proc, entry.address, &val, entry.byteValue, nullptr);
    }
    }
    return false;
}

DWORD WINAPI freezeValue(LPVOID param)
{
    FreezeParams* p = (FreezeParams*)param;

    while (p->running)
    {
        writeValue(p->entry->pFhandle, *p->entry, p->entry->oldValue);
        Sleep(50);
    }

    return 0;
}