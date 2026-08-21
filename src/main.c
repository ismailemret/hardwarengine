#include <stdio.h>
#include <windows.h>
#include "../include/telemetry.h"

int main(void) {
    printf("=== Hardware Engine - Core Telemetry ===\n");

    RAMStatus ram = {0};
    CpuTelemetry cpu = {0};

    bool driver_loaded = init_kernel_driver();
    if (driver_loaded) {
        printf("[BILGI] Kernel surucusu baglandi (Ring 0 aktif).\n");
    } else {
        printf("[UYARI] Kernel surucusu bulunamadi! (Yalnizca Ring 3 OS metrikleri calisacak)\n");
    }

    uint64_t msr_1a2 = driver_loaded ? read_msr_driver(0, 0x1A2) : 0;
    if ((msr_1a2 >> 16 & 0xFF) == 0) {
        msr_1a2 = (uint64_t)100 << 16;
    }

    if (!init_cpu_sensor(&cpu, msr_1a2)) {
        printf("[HATA] CPU sensoru baslatilamadi!\n");
        return 1;
    }
    printf("[BILGI] CPU sensoru baslatildi. Aktif Cekirdek: %u | TjMax: %u C\n", cpu.active_core_count, cpu.tjmax);

    update_cpu_telemetry(&cpu, driver_loaded ? read_msr_driver : NULL);

    while (1) {
        Sleep(500);

        get_ram_status(&ram);
        update_cpu_telemetry(&cpu, driver_loaded ? read_msr_driver : NULL);

        double used_gb  = (double)ram.used_ram  / (1024.0 * 1024.0 * 1024.0);
        double total_gb = (double)ram.total_ram / (1024.0 * 1024.0 * 1024.0);

        printf("\r[RAM] %3lu%% (%5.2f/%5.2f GB) | [CPU] %5.1f%% | Temp: %4.1f C | Volt: %5.3f V | Throttle: %s",
               ram.memory_load,
               used_gb,
               total_gb,
               cpu.total_usage_pct,
               cpu.package_temp_c,
               cpu.max_voltage_v,
               cpu.cores[0].is_throttling ? "YES" : "NO ");
        
        fflush(stdout);
    }

    close_kernel_driver();
    return 0;
}