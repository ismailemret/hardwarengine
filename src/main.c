#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "../include/telemetry.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#define SAMPLE_COUNT 20 // 20 samples * 100ms = 2000ms (2.0s window)

typedef struct {
    float usage_pct;
    float temp_c;
    float freq_mhz;
    float voltage_v;
} CoreAccumulator;

typedef struct {
    float total_usage_pct;
    float package_temp_c;
    float max_voltage_v;
    CoreAccumulator cores[MAX_LOGICAL_CORES];
    
    // RAM Metrics
    uint64_t memory_load;
    uint64_t used_ram;
    uint64_t total_ram;

    // Self Profiling Metrics
    float self_cpu_pct;
    uint64_t self_working_set;
    uint64_t self_private;
} TelemetryAccumulator;

int main(void) {
    CpuVendor vendor = detect_cpu_vendor();
    if (!init_kernel_driver(vendor)) {
        printf("[CRITICAL] PawnIO initialization failed. Install PawnIO and its MSR module.\n");
        return -1;
    }

    CpuTelemetry cpu;
    RAMStatus ram;
    SelfMetrics self;

    if (!init_cpu_sensor(&cpu, read_msr_driver)) {
        printf("[ERROR] CPU sensor initialization failed.\n");
        close_kernel_driver();
        return -1;
    }

    init_self_metrics(&self);

    // High-resolution periodic waitable timer (100ms / 10 Hz)
    HANDLE hTimer = CreateWaitableTimerExW(
        NULL,
        NULL,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS
    );

    if (!hTimer) {
        hTimer = CreateWaitableTimerW(NULL, FALSE, NULL);
    }

    LARGE_INTEGER due_time;
    due_time.QuadPart = -1000000LL; // 100ms relative offset (in 100ns units)
    SetWaitableTimer(hTimer, &due_time, 100, NULL, NULL, FALSE);

    TelemetryAccumulator acc;
    memset(&acc, 0, sizeof(TelemetryAccumulator));
    uint32_t sample_idx = 0;

    for (;;) {
        // Precise kernel wait (100ms tick)
        WaitForSingleObject(hTimer, INFINITE);

        update_cpu_telemetry(&cpu, read_msr_driver);
        get_ram_status(&ram);
        update_self_metrics(&self, cpu.active_core_count);

        // Accumulate raw sample data
        acc.total_usage_pct += cpu.total_usage_pct;
        acc.package_temp_c  += cpu.package_temp_c;
        acc.max_voltage_v   += cpu.max_voltage_v;

        for (uint32_t i = 0; i < cpu.active_core_count; i++) {
            acc.cores[i].usage_pct += cpu.cores[i].usage_pct;
            acc.cores[i].temp_c    += cpu.cores[i].temp_c;
            acc.cores[i].freq_mhz  += cpu.cores[i].freq_mhz;
            acc.cores[i].voltage_v += cpu.cores[i].voltage_v;
        }

        acc.memory_load += ram.memory_load;
        acc.used_ram    += ram.used_ram;
        acc.total_ram   += ram.total_ram;

        acc.self_cpu_pct     += self.cpu_usage_pct;
        acc.self_working_set += self.working_set_bytes;
        acc.self_private     += self.private_bytes;

        sample_idx++;

        // Render averaged metrics every 20 samples (2.0s)
        if (sample_idx >= SAMPLE_COUNT) {
            float inv_samples = 1.0f / (float)SAMPLE_COUNT;

            system("cls");
            printf("================= hardwarengine CPU & RAM Telemetry (2s Avg) =================\n");
            printf("CPU: %s [%s]\n", cpu.brand_string, cpu.vendor == CPU_VENDOR_INTEL ? "Intel" : "AMD");
            printf("Topology: %u Physical Cores / %u Logical Threads | TjMax: %u C\n", 
                   cpu.physical_core_count, cpu.active_core_count, cpu.tjmax);
            printf("------------------------------------------------------------------------------\n");
            printf("CPU Load: %5.1f%% | Package Temp: %4.1f C | Peak Voltage: %.3f V\n", 
                   acc.total_usage_pct * inv_samples,
                   acc.package_temp_c * inv_samples,
                   acc.max_voltage_v * inv_samples);

            printf("-------------------------------- Per-Core Telemetry -----------------------------\n");
            for (uint32_t i = 0; i < cpu.active_core_count; i++) {
                printf(" Core %02u [%s] | %6.1f MHz | %4.1f C | %.3f V | Load: %5.1f%% %s\n",
                       cpu.cores[i].core_id,
                       cpu.cores[i].type == CORE_TYPE_PERFORMANCE ? "P" : "E",
                       acc.cores[i].freq_mhz * inv_samples,
                       acc.cores[i].temp_c * inv_samples,
                       acc.cores[i].voltage_v * inv_samples,
                       acc.cores[i].usage_pct * inv_samples,
                       cpu.cores[i].is_throttling ? "[PROCHOT]" : "");
            }

            printf("-------------------------------- System RAM -------------------------------------\n");
            printf(" Load: %llu%% | Used: %llu MB / %llu MB\n",
                   (uint64_t)(acc.memory_load * inv_samples),
                   (uint64_t)((acc.used_ram * inv_samples) / (1024 * 1024)),
                   (uint64_t)((acc.total_ram * inv_samples) / (1024 * 1024)));

            printf("----------------------------- Process Self-Profile ------------------------------\n");
            printf(" Engine CPU Overhead: %6.3f%% | Working Set: %6.2f KB | Private Commit: %6.2f KB\n",
                   acc.self_cpu_pct * inv_samples,
                   ((double)acc.self_working_set * inv_samples) / 1024.0,
                   ((double)acc.self_private * inv_samples) / 1024.0);
            printf("=================================================================================\n");

            // Reset accumulator and counter for the next window
            memset(&acc, 0, sizeof(TelemetryAccumulator));
            sample_idx = 0;
        }
    }

    return 0;
}