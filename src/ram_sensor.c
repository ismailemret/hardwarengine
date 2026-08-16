#include "../include/telemetry.h"

int get_ram_status(RAMStatus *status) {
    if (status == NULL) {
        return -1;
    }

    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);

    if (!GlobalMemoryStatusEx(&mem_status)) {
        return -1;
    }

    status->memory_load = mem_status.dwMemoryLoad;
    status->total_ram = mem_status.ullTotalPhys;
    status->avail_ram = mem_status.ullAvailPhys;
    status->used_ram = mem_status.ullTotalPhys - mem_status.ullAvailPhys;

    return 0;
}