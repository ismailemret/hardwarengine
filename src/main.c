#include <stdio.h>
#include <windows.h>
#include "../include/telemetry.h"

int main(void) {
    printf("=== Hardware Engine - Core Telemetry ===\n");

    RAMStatus ram = {0};

    while (1) {
        if (get_ram_status(&ram) == 0) {
            double used_gb = (double)ram.used_ram / (1024.0 * 1024.0 * 1024.0);
            double total_gb = (double)ram.total_ram / (1024.0 * 1024.0 * 1024.0);
            double avail_gb = (double)ram.avail_ram / (1024.0 * 1024.0 * 1024.0);

            printf("\r[RAM] Load: %3u%% | Used: %5.2f GB / %5.2f GB | Avail: %5.2f GB",
                   ram.memory_load, used_gb, total_gb, avail_gb);
            fflush(stdout);
        }

        Sleep(250);
    }

    return 0;
}