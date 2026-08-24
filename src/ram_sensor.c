#include "../include/telemetry.h"

int get_ram_status(RAMStatus *status) {
    if (!status) return 0;

    MEMORYSTATUSEX mem_stat;
    mem_stat.dwLength = sizeof(MEMORYSTATUSEX);

    if (!GlobalMemoryStatusEx(&mem_stat)) {
        return 0;
    }

    status->memory_load = mem_stat.dwMemoryLoad;
    status->total_ram   = mem_stat.ullTotalPhys;
    status->avail_ram   = mem_stat.ullAvailPhys;
    status->used_ram    = mem_stat.ullTotalPhys - mem_stat.ullAvailPhys;

    return 1;
}