#pragma once
#include "structs.h"

double readValue(HANDLE proc, ScanEntry& entry);
bool writeValue(HANDLE proc, ScanEntry& entry, double newValue);

DWORD WINAPI freezeValue(LPVOID param);