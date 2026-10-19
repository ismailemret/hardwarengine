#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "../include/telemetry.h"
#ifdef ENABLE_GUI
#include "../include/gui.h"
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#define SAMPLE_COUNT 20

#ifndef ENABLE_GUI
typedef struct {
    float usage_pct;
    float temp_c;
    float freq_mhz;
    float voltage_v;
    bool is_thermal_throttling;
    bool is_power_throttling;
} CoreAccumulator;

typedef struct {
    float core_util_pct;
    float memory_util_pct;
    float core_clock_mhz;
    float memory_clock_mhz;
    float temp_c;
    float power_w;
    float fan_speed_pct;
    uint64_t vram_used_bytes;
    uint64_t vram_total_bytes;
} GpuDeviceAccumulator;

typedef struct {
    float total_usage_pct;
    float package_temp_c;
    float package_power_w;
    float max_voltage_v;
    CoreAccumulator cores[MAX_LOGICAL_CORES];
    uint64_t memory_load;
    uint64_t total_phys_ram;
    uint64_t used_phys_ram;
    uint64_t hardware_reserved_bytes;
    uint64_t paged_pool_bytes;
    uint64_t non_paged_pool_bytes;
    uint64_t commit_total_bytes;
    uint64_t commit_limit_bytes;
    float self_cpu_pct;
    uint64_t self_working_set;
    uint64_t self_private;
    GpuDeviceAccumulator gpus[MAX_GPUS];
} TelemetryAccumulator;

static volatile LONG g_running = 1;

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT ||
        ctrl_type == CTRL_CLOSE_EVENT || ctrl_type == CTRL_SHUTDOWN_EVENT) {
        InterlockedExchange(&g_running, 0);
        return TRUE;
    }
    return FALSE;
}

static void cli_render_snapshot(const TelemetryAccumulator *acc, const CpuTelemetry *cpu,
                                const GpuTelemetry *gpu, int gpu_available, uint32_t sample_count) {
    if (sample_count == 0) return;
    float inv = 1.0f / (float)sample_count;
    double to_mib = 1.0 / (1024.0 * 1024.0);

    printf("\033[2J\033[H");
    printf("================================================================================\n");
    printf(" hardwarengine | Low-Overhead Hardware Telemetry (Window: %u samples / 2s)\n", sample_count);
    printf("================================================================================\n");
    printf(" CPU : %s (%uP / %uL Cores | TjMax: %u C)\n",
           cpu->brand_string, cpu->physical_core_count, cpu->active_core_count, cpu->tjmax);
    printf(" Load: %5.1f%% | Pkg Temp: %5.1f C | Pkg Power: %6.2f W | Max Vcore: %.3f V\n",
           acc->total_usage_pct * inv, acc->package_temp_c * inv,
           acc->package_power_w * inv, acc->max_voltage_v * inv);
    printf("--------------------------------------------------------------------------------\n");

    for (uint32_t i = 0; i < cpu->active_core_count && i < MAX_LOGICAL_CORES; ++i) {
        printf(" Core %02u [%c] : %5.1f%% | %7.1f MHz | %5.1f C | %.3f V | Throt[T:%d P:%d]\n",
               cpu->cores[i].core_id,
               cpu->cores[i].type == CORE_TYPE_PERFORMANCE ? 'P' : 'E',
               acc->cores[i].usage_pct * inv,
               acc->cores[i].freq_mhz * inv,
               acc->cores[i].temp_c * inv,
               acc->cores[i].voltage_v * inv,
               acc->cores[i].is_thermal_throttling ? 1 : 0,
               acc->cores[i].is_power_throttling ? 1 : 0);
    }

    printf("--------------------------------------------------------------------------------\n");
    printf(" RAM : %llu%% Load | Used: %.0f / %.0f MiB | Commit: %.0f / %.0f MiB | NonPaged: %.1f MiB\n",
           (unsigned long long)(acc->memory_load / sample_count),
           (double)(acc->used_phys_ram / sample_count) * to_mib,
           (double)(acc->total_phys_ram / sample_count) * to_mib,
           (double)(acc->commit_total_bytes / sample_count) * to_mib,
           (double)(acc->commit_limit_bytes / sample_count) * to_mib,
           (double)(acc->non_paged_pool_bytes / sample_count) * to_mib);

    if (gpu_available && gpu->gpu_count > 0) {
        bool has_active = false;
        for (uint32_t i = 0; i < gpu->gpu_count && i < MAX_GPUS; ++i) {
            if (!gpu->gpus[i].is_active) continue;
            has_active = true;
            printf(" GPU %u (%s) : %4.1f%% | %4.1f C | %5.1f W | Clock: %.0f MHz | VRAM: %.0f / %.0f MiB\n",
                   i, gpu->gpus[i].name,
                   acc->gpus[i].core_util_pct * inv,
                   acc->gpus[i].temp_c * inv,
                   acc->gpus[i].power_w * inv,
                   acc->gpus[i].core_clock_mhz * inv,
                   (double)(acc->gpus[i].vram_used_bytes / sample_count) * to_mib,
                   (double)(acc->gpus[i].vram_total_bytes / sample_count) * to_mib);
        }
        if (!has_active) {
            printf(" GPU : Discrete GPU in RTD3 / D3Cold low-power sleep state\n");
        }
    } else {
        printf(" GPU : NVML unavailable\n");
    }

    printf("--------------------------------------------------------------------------------\n");
    printf(" Self: CPU Overhead: %.3f%% | Working Set: %.1f KiB | Private: %.1f KiB\n",
           acc->self_cpu_pct * inv,
           (double)(acc->self_working_set / sample_count) / 1024.0,
           (double)(acc->self_private / sample_count) / 1024.0);
    printf("================================================================================\n");
    fflush(stdout);
}
#endif

static int run_engine(HINSTANCE instance) {
    CpuVendor vendor = detect_cpu_vendor();
    if (!init_kernel_driver(vendor)) {
#ifdef ENABLE_GUI
        MessageBoxA(NULL, "PawnIO initialization failed. Install PawnIO and its MSR module.",
                    "hardwarengine", MB_ICONERROR);
#else
        fprintf(stderr, "[ERROR] PawnIO initialization failed. Install PawnIO and its MSR module.\n");
#endif
        return -1;
    }

    CpuTelemetry cpu;
    RAMStatus ram;
    SelfMetrics self;
    GpuTelemetry gpu_data;
    if (!init_cpu_sensor(&cpu, read_msr_driver)) {
#ifdef ENABLE_GUI
        MessageBoxA(NULL, "CPU sensor initialization failed.", "hardwarengine", MB_ICONERROR);
#else
        fprintf(stderr, "[ERROR] CPU sensor initialization failed.\n");
#endif
        close_kernel_driver();
        return -1;
    }

    int gpu_available = init_gpu_sensor(&gpu_data);
    init_self_metrics(&self);

#ifdef ENABLE_GUI
    if (!gui_init(instance)) {
        MessageBoxA(NULL, "Telemetry window initialization failed.", "hardwarengine", MB_ICONERROR);
        shutdown_gpu_sensor();
        close_kernel_driver();
        return -1;
    }
#else
    (void)instance;
    HANDLE std_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (std_out != INVALID_HANDLE_VALUE) {
        DWORD console_mode = 0;
        if (GetConsoleMode(std_out, &console_mode)) {
            SetConsoleMode(std_out, console_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#endif

    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    if (!timer) {
#ifdef ENABLE_GUI
        gui_destroy();
#else
        SetConsoleCtrlHandler(console_ctrl_handler, FALSE);
#endif
        shutdown_gpu_sensor();
        close_kernel_driver();
        return -1;
    }

    LARGE_INTEGER due_time;
    due_time.QuadPart = -1000000LL;
    SetWaitableTimer(timer, &due_time, 100, NULL, NULL, FALSE);

    TelemetryAccumulator acc;
    memset(&acc, 0, sizeof(acc));
    uint32_t sample_idx = 0;
    bool running = true;

    while (running) {
#ifdef ENABLE_GUI
        running = gui_pump_messages();
        if (!running) break;
        WaitForSingleObject(timer, 100);
        running = gui_pump_messages();
        if (!running) break;
#else
        if (InterlockedCompareExchange(&g_running, 1, 1) == 0) break;
        WaitForSingleObject(timer, 100);
        if (InterlockedCompareExchange(&g_running, 1, 1) == 0) break;
#endif

        update_cpu_telemetry(&cpu, read_msr_driver);
        get_ram_status(&ram);
        update_self_metrics(&self, cpu.active_core_count);
        if (gpu_available) update_gpu_telemetry(&gpu_data);

        acc.total_usage_pct += cpu.total_usage_pct;
        acc.package_temp_c += cpu.package_temp_c;
        acc.package_power_w += cpu.package_power_w;
        acc.max_voltage_v += cpu.max_voltage_v;
        for (uint32_t i = 0; i < cpu.active_core_count && i < MAX_LOGICAL_CORES; ++i) {
            acc.cores[i].usage_pct += cpu.cores[i].usage_pct;
            acc.cores[i].temp_c += cpu.cores[i].temp_c;
            acc.cores[i].freq_mhz += cpu.cores[i].freq_mhz;
            acc.cores[i].voltage_v += cpu.cores[i].voltage_v;
            if (cpu.cores[i].is_thermal_throttling) acc.cores[i].is_thermal_throttling = true;
            if (cpu.cores[i].is_power_throttling) acc.cores[i].is_power_throttling = true;
        }
        acc.memory_load += ram.memory_load;
        acc.total_phys_ram += ram.total_phys_ram;
        acc.used_phys_ram += ram.used_phys_ram;
        acc.hardware_reserved_bytes += ram.hardware_reserved_bytes;
        acc.paged_pool_bytes += ram.paged_pool_bytes;
        acc.non_paged_pool_bytes += ram.non_paged_pool_bytes;
        acc.commit_total_bytes += ram.commit_total_bytes;
        acc.commit_limit_bytes += ram.commit_limit_bytes;
        acc.self_cpu_pct += self.cpu_usage_pct;
        acc.self_working_set += self.working_set_bytes;
        acc.self_private += self.private_bytes;
        if (gpu_available) {
            for (uint32_t i = 0; i < gpu_data.gpu_count && i < MAX_GPUS; ++i) {
                if (!gpu_data.gpus[i].is_active) continue;
                acc.gpus[i].core_util_pct += gpu_data.gpus[i].core_util_pct;
                acc.gpus[i].memory_util_pct += gpu_data.gpus[i].memory_util_pct;
                acc.gpus[i].core_clock_mhz += (float)gpu_data.gpus[i].core_clock_mhz;
                acc.gpus[i].memory_clock_mhz += (float)gpu_data.gpus[i].memory_clock_mhz;
                acc.gpus[i].temp_c += gpu_data.gpus[i].temp_c;
                acc.gpus[i].power_w += gpu_data.gpus[i].power_w;
                acc.gpus[i].fan_speed_pct += (float)gpu_data.gpus[i].fan_speed_pct;
                acc.gpus[i].vram_used_bytes += gpu_data.gpus[i].vram_used_bytes;
                acc.gpus[i].vram_total_bytes += gpu_data.gpus[i].vram_total_bytes;
            }
        }

        if (++sample_idx >= SAMPLE_COUNT) {
#ifdef ENABLE_GUI
            gui_update_snapshot(&acc, &cpu, &gpu_data, gpu_available, sample_idx, &ram, &self);
            gui_request_redraw();
#else
            cli_render_snapshot(&acc, &cpu, &gpu_data, gpu_available, sample_idx);
#endif
            memset(&acc, 0, sizeof(acc));
            sample_idx = 0;
        }
    }

    CancelWaitableTimer(timer);
    CloseHandle(timer);
    shutdown_gpu_sensor();
    close_kernel_driver();
#ifdef ENABLE_GUI
    gui_destroy();
#else
    SetConsoleCtrlHandler(console_ctrl_handler, FALSE);
#endif
    return 0;
}

#ifdef ENABLE_GUI
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance, LPSTR command_line, int show_command) {
    (void)previous_instance;
    (void)command_line;
    (void)show_command;
    return run_engine(instance);
}
#else
int main(void) {
    return run_engine(GetModuleHandleA(NULL));
}
#endif