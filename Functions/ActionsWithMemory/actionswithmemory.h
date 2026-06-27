#pragma once

#include "structs.h"

void actionFirstScan(ScanContext&);
void actionNextScan(ScanContext&);
void actionChangeValue(ScanContext&);
void actionFreezeValue(ScanContext&);
void actionUnfreezeValue(ScanContext&);
void actionShowFoundedAddresses(ScanContext&);
void actionSaveFoundedAddresses(ScanContext&, const char*);
void actionLoadFoundedAddresses(ScanContext&, const char*);