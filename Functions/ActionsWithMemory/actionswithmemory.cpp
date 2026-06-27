#include "actionswithmemory.h"
#include "readwritememory.h"
#include "showdata.h"

void actionFirstScan(ScanContext& sContext)
{
    if (!sContext.freezeParams.empty()) {
        puts("Unfreezing all values in list");
        // Останавливаем все заморозки перед очисткой
        for (size_t i = 0; i < sContext.freezeParams.size(); i++) {
            sContext.freezeParams[i]->running = false;
            WaitForSingleObject(sContext.freezeThreads[i], INFINITE);
            CloseHandle(sContext.freezeThreads[i]);
            delete sContext.freezeParams[i];
        }
        sContext.freezeThreads.clear();
        sContext.freezeParams.clear();
    }


    unsigned char* minAddr = (unsigned char*)sContext.sInfo.lpMinimumApplicationAddress;
    unsigned char* maxAddr = (unsigned char*)sContext.sInfo.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi;

    double searchValue = 0.0;

    printf("\nSelect data type:");
    printf("\n1. short(2 bytes)\n");
    printf("2. int(4 bytes)\n");
    printf("3. float(4 bytes)\n");
    printf("4. double(8 bytes)\n");
    printf("Enter value: ");
    scanf_s("%d", &sContext.choiceType);

    // Записываем выбранный тип данных для поиска
    switch (sContext.choiceType) {
    case 1: sContext.foundType = sizeof(short);
        break;
    case 2: sContext.foundType = sizeof(int);
        break;
    case 3: sContext.foundType = sizeof(float);
        break;
    case 4: sContext.foundType = sizeof(double);
        break;
    default:
        printf("Unknown data type\n");
        return;
    }

    // Действия для каждого нового выбора first scan
    sContext.foundAddresses.clear();

    printf("\n\nEnter search value: ");
    scanf_s("%lf", &searchValue);

    while (minAddr < maxAddr && VirtualQueryEx(sContext.procHandle, minAddr, &mbi, sizeof(mbi))) // Прохожу по памяти процесса
    {
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) // Проверяю на стейт памяти(на то, что оно выделена и можно работать)
        {
            printf("Memory region: 0x%p | Size: %zu bytes\n", mbi.BaseAddress, mbi.RegionSize); // Вывод читаемого региона памяти и его размера

            BYTE* buffer = (BYTE*)malloc(mbi.RegionSize); // Временный буфер, для хранения данных куда будут записаны данные при чтении в ReadProcessMemory
            if (buffer == nullptr)
            {
                printf("Failed to allocate buffer for region %p\n", mbi.BaseAddress);
                minAddr = (unsigned char*)mbi.BaseAddress + mbi.RegionSize; // Переход к следующему региону памяти
                continue;
            }

            SIZE_T bytesRead = 0; // Запишет сколько байт прочитано в ReadProcessMemory для страницы вирт.памяти
            if (ReadProcessMemory(sContext.procHandle, mbi.BaseAddress, buffer, mbi.RegionSize, &bytesRead)) // Читает каждый байт начиная с BaseAddress и записывая в buffer в этом диапазоне RegionSize
            {
                for (size_t i = 0; i + sContext.foundType <= bytesRead; i += sContext.foundType) // Двигаемся по буферу с условием, что читаемый байт меньше записанного bytesRead
                {
                    bool match = false; // Переменная для проверки на найденное число
                    unsigned char* addr = (unsigned char*)mbi.BaseAddress + i; // Получаем адрес значения по "формуле" - mbi.BaseAddress(начало региона) + i(текущий индекс элемента в буфере)

                    // Проверка на значение.
                    // buffer + i - элемент буфера
                    // (type*)(buffer + i) - приводим к указателю(обязательно)
                    // *(type*)(buffer + i) - получаем значение

                    switch (sContext.choiceType) {
                    case 1:
                        if (*(short*)(buffer + i) == (short)searchValue)
                            match = true;
                        break;
                    case 2:
                        if (*(int*)(buffer + i) == (int)searchValue)
                            match = true;
                        break;
                    case 3:
                        if (fabs(*(float*)(buffer + i) - (float)searchValue) < 0.001f) // Проверка с эпсилоном, то есть считает совпадением, если разница меньше 0.001
                            match = true;
                        break;
                    case 4:
                        if (fabs(*(double*)(buffer + i) - (double)searchValue) < 0.001) // Проверка с эпсилоном, то есть считает совпадением, если разница меньше 0.001
                            match = true;
                        break;
                    }

                    if (match)
                    {
                        ScanEntry entry;
                        entry.address = addr;
                        entry.oldValue = searchValue;
                        entry.typeValue = sContext.choiceType;
                        entry.byteValue = sContext.foundType;
                        entry.pFhandle = sContext.procHandle;

                        sContext.foundAddresses.push_back(entry); // Добавляем в конец вектора

                        printf("---FOUND--- at address: 0x%p\n", addr);
                    }
                }
            }

            free(buffer);
        }

        minAddr = (unsigned char*)mbi.BaseAddress + mbi.RegionSize; // Переходим в другой регион, если память не соответствует условию
    }
    printf("First Scan done. Found: %zu addresses\n", sContext.foundAddresses.size());
}

void actionNextScan(ScanContext& sContext)
{
    if (sContext.foundAddresses.empty()) // Проверка на пустой вектор
    {
        puts("Run First Scan first");
        return;
    }

    int scanType = 0;
    printf("\nSelect Next Scan type:\n");
    printf("1. Increased\n");
    printf("2. Decreased\n");
    printf("3. Exact Value\n");
    printf("4. Changed\n");
    printf("5. Unchanged\n");
    printf("Choice: ");
    scanf_s("%d", &scanType);

    double compareValue = 0.0;
    if (scanType == 3)
    {
        printf("\n\nEnter new search value: ");
        scanf_s("%lf", &compareValue);
    }

    std::vector<ScanEntry> newList; // Вектор, в который записываем новые адреса, у которых совпало значение

    if (scanType < 1 || scanType > 5) {
        puts("Input number isn't range between 1 and 5");
        return;
    }


    // Перебираем и отсеиваем несовпавшие значения
    for (auto& addr : sContext.foundAddresses)
    {
        bool match = false;
        double current = 0.0;

        // Читаем текущее значение
        current = readValue(sContext.procHandle, addr);


        // Проверки на подтип next scan. Increased, Decreased, Exact
        bool isFloat = (addr.typeValue == 3 || addr.typeValue == 4);

        switch (scanType) {
        case 1:
            if (current > addr.oldValue) match = true;
            break;
        case 2:
            if (current < addr.oldValue) match = true;
            break;
        case 3:
            if (isFloat)
                match = fabs(current - compareValue) < 0.001;
            else
                match = (current == compareValue);
            break;
        case 4:
            if (isFloat)
                match = fabs(current - addr.oldValue) >= 0.001;
            else
                match = (current != addr.oldValue);
            break;
        case 5:
            if (isFloat)
                match = fabs(current - addr.oldValue) < 0.001;
            else
                match = (current == addr.oldValue);
            break;
        }

        if (match)
        {
            addr.oldValue = current;
            newList.push_back(addr);
            printf("Match: 0x%p (%.2f)\n", addr.address, current);
        }
    }

    sContext.foundAddresses = move(newList); // Передаём контроль над newList в foundAddresses
    printf("After Next Scan: %zu addresses left\n", sContext.foundAddresses.size());
}

void actionChangeValue(ScanContext& sContext)
{
    if (sContext.foundAddresses.empty())
    {
        puts("Scan memory first");
        return;
    }

    puts("Memory cells:");
    for (size_t i = 0; i < sContext.foundAddresses.size(); i++)
    {
        double current = readValue(sContext.procHandle, sContext.foundAddresses[i]);
        printf("[%zu] 0x%p | %.2f\n", i + 1, sContext.foundAddresses[i].address, current);
    }

    size_t index = 0;
    printf("\nEnter cell number [1 - %zu]: ", sContext.foundAddresses.size());
    scanf_s("%zu", &index);

    if (index < 1 || index > sContext.foundAddresses.size())
    {
        puts("Invalid cell number");
        return;
    }

    size_t idx = index - 1;
    double newValue = 0.0;
    printf("New value: ");
    scanf_s("%lf", &newValue);

    bool written = writeValue(sContext.procHandle, sContext.foundAddresses[idx], newValue);

    if (written)
        printf("Value %.2f written to 0x%p\n", newValue, sContext.foundAddresses[idx].address);
    else
        printf("Failed to write value. Error: %lu\n", GetLastError());
}

void actionFreezeValue(ScanContext& sContext)
{
    if (sContext.foundAddresses.empty())
    {
        puts("No values to freeze.");
        return;
    }

    showAddresses(sContext.foundAddresses);

    size_t index = 0;
    printf("\nEnter cell number [1 - %zu]: ", sContext.foundAddresses.size());
    scanf_s("%zu", &index);

    if (index < 1 || index > sContext.foundAddresses.size())
    {
        puts("Invalid cell number");
        return;
    }

    size_t idx = index - 1;

    for (auto* fp : sContext.freezeParams) {
        if (fp->entry->address == sContext.foundAddresses[idx].address) {
            puts("This address is already frozen");
            return;
        }
    }

    FreezeParams* fp = new FreezeParams();
    fp->entry = &sContext.foundAddresses[idx];
    fp->running = true;

    sContext.freezeThreads.push_back(CreateThread(NULL, 0, freezeValue, fp, 0, NULL));
    sContext.freezeParams.push_back(fp);
}

void actionUnfreezeValue(ScanContext& sContext)
{
    if (sContext.freezeThreads.empty()) {
        puts("Nothing is frozen");
        return;
    }

    printf("Frozen values:\n");
    for (size_t i = 0; i < sContext.freezeParams.size(); i++) {
        printf("[%zu] 0x%p | %.2f\n", i + 1,
            sContext.freezeParams[i]->entry->address,
            sContext.freezeParams[i]->entry->oldValue);
    }

    size_t index = 0;
    printf("\nEnter cell number [1 - %zu]: ", sContext.freezeParams.size());
    scanf_s("%zu", &index);

    if (index < 1 || index > sContext.freezeParams.size()) {
        puts("Invalid cell number");
        return;
    }

    size_t idx = index - 1;

    sContext.freezeParams[idx]->running = false;
    WaitForSingleObject(sContext.freezeThreads[idx], INFINITE);
    CloseHandle(sContext.freezeThreads[idx]);
    delete sContext.freezeParams[idx];

    sContext.freezeThreads.erase(sContext.freezeThreads.begin() + idx);
    sContext.freezeParams.erase(sContext.freezeParams.begin() + idx);

    puts("Value unfrozen");
}

void actionShowFoundedAddresses(ScanContext& sContext)
{
    if (sContext.foundAddresses.empty())
    {
        puts("No addresses to show.");
        return;
    }

    printf("Total addresses: %zu\n\n", sContext.foundAddresses.size());

    showAddresses(sContext.foundAddresses);
}

void actionSaveFoundedAddresses(ScanContext& sContext, const char* SAVE_FILE)
{
    if (sContext.foundAddresses.empty()) {
        puts("No addresses to save");
        return;
    }

    errno_t err;
    FILE* f;

    err = fopen_s(&f, SAVE_FILE, "w");

    if (err != 0 || !f) {
        puts("Failed to create file");
        return;
    }

    fprintf(f, "%zu\n", sContext.foundAddresses.size());

    for (auto& entry : sContext.foundAddresses) {
        fprintf(f, "%llx %d %zu %.15g\n", (unsigned long long)entry.address, entry.typeValue, entry.byteValue, entry.oldValue);
    }

    fclose(f);
    printf("Saved %zu addresses to %s\n", sContext.foundAddresses.size(), SAVE_FILE);
}

void actionLoadFoundedAddresses(ScanContext& sContext, const char* SAVE_FILE)
{
    errno_t err;
    FILE* f;
    
    err = fopen_s(&f, SAVE_FILE, "r");

    if (err != 0 || !f) {
        puts("File adres.txt not found");
        return;
    }

    sContext.foundAddresses.clear();
    size_t count = 0;

    if (fscanf_s(f, "%zu", &count) != 1) {
        puts("Failed to read file header");
        fclose(f);
        return;
    }

    for (size_t i = 0; i < count; i++) {
        ScanEntry entry = {};
        unsigned long long addr_temp = 0;

        if (fscanf_s(f, "%llx %d %zu %lf", &addr_temp, &entry.typeValue, &entry.byteValue, &entry.oldValue) == 4)
        {
            entry.address = (BYTE*)addr_temp;
            entry.pFhandle = sContext.procHandle;
            sContext.foundAddresses.push_back(entry);
        }
    }

    fclose(f);
    printf("Loaded %zu addresses from %s\n", sContext.foundAddresses.size(), SAVE_FILE);
}