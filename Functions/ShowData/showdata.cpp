#include <stdio.h>
#include "showdata.h"

void showAddresses(const std::vector<ScanEntry>& foundAddresses)
{
    size_t num = 1;
    for (auto& addr : foundAddresses)
    {
        printf("%2zu. 0x%p | Type: %d | Old value: %.2f\n", num++, addr.address, addr.typeValue, addr.oldValue);
    }
}