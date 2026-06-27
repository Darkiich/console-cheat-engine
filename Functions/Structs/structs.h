#pragma once
#include <windows.h>
#include <vector>

union TV {
    short s;
    int i;
    float f;
    double d;
};

typedef struct SE {
    BYTE* address;
    double oldValue;
    int typeValue;
    size_t byteValue;
    TV tv;
    HANDLE pFhandle;
} ScanEntry;

typedef struct FP {
    ScanEntry* entry;
    volatile bool running;
} FreezeParams;

typedef struct ScanContext {
    HANDLE procHandle;
    SYSTEM_INFO sInfo;
    std::vector<ScanEntry> foundAddresses;

    std::vector<HANDLE> freezeThreads;
    std::vector<FreezeParams*> freezeParams;

    int choiceType;
    size_t foundType;
} ScanContext;