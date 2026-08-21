#include "../include/telemetry.h"
#include <windows.h>
#include <winternl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define SystemProcessorPerformanceInformation 8

typedef NTSTATUS(NTAPI *pfnNtQuerySystemInformation)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

// Intel MSR Adresleri
#define MSR_IA32_PERF_STATUS        0x0198
#define MSR_IA32_THERM_STATUS       0x019C
#define MSR_IA32_TEMPERATURE_TARGET 0x01A2
#define MSR_IA32_MPERF              0x00E7
#define MSR_IA32_APERF              0x00E8

static inline uint32_t extract_tjmax(uint64_t msr_1a2) {
    return (uint32_t)((msr_1a2 >> 16) & 0xFF);
}

static inline bool extract_core_temp(uint64_t msr_19c, uint32_t tjmax, float *temp_out) {
    if (!((msr_19c >> 31) & 0x1)) return false;
    uint32_t delta = (uint32_t)((msr_19c >> 16) & 0x7F);
    *temp_out = (float)(tjmax - delta);
    return true;
}

static inline bool extract_throttling(uint64_t msr_19c) {
    return (bool)((msr_19c >> 2) & 0x1);
}

static inline float extract_voltage(uint64_t msr_198) {
    uint32_t vid = (uint32_t)((msr_198 >> 32) & 0xFFFF);
    return (float)vid / 8192.0f;
}

static pfnNtQuerySystemInformation NtQuerySysInfo = NULL;

int init_cpu_sensor(CpuTelemetry *telemetry, uint64_t msr_1a2_val) {
    if (!telemetry) return 0;

    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) {
        hNtdll = LoadLibraryA("ntdll.dll");
    }
    if (!hNtdll) return 0;

    NtQuerySysInfo = (pfnNtQuerySystemInformation)GetProcAddress(hNtdll, "NtQuerySystemInformation");
    if (!NtQuerySysInfo) return 0;

    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    telemetry->active_core_count = sys_info.dwNumberOfProcessors;
    if (telemetry->active_core_count > MAX_LOGICAL_CORES) {
        telemetry->active_core_count = MAX_LOGICAL_CORES;
    }

    telemetry->base_bclk_mhz = 100;
    telemetry->tjmax = extract_tjmax(msr_1a2_val);

    for (uint32_t i = 0; i < telemetry->active_core_count; i++) {
        telemetry->cores[i].core_id = i;
        telemetry->cores[i].usage_pct = 0.0f;
        telemetry->cores[i].temp_c = 0.0f;
        telemetry->cores[i].voltage_v = 0.0f;
        telemetry->cores[i].freq_mhz = 0.0f;
        telemetry->cores[i].is_throttling = false;
        telemetry->cores[i].prev_idle_time = 0;
        telemetry->cores[i].prev_kernel_time = 0;
        telemetry->cores[i].prev_user_time = 0;
        telemetry->cores[i].prev_mperf = 0;
        telemetry->cores[i].prev_aperf = 0;
    }

    return 1;
}

void update_cpu_telemetry(CpuTelemetry *telemetry, msr_reader_cb read_msr) {
    if (!telemetry || !NtQuerySysInfo) return;

    SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION perf_info[MAX_LOGICAL_CORES];
    ULONG return_len = 0;

    NTSTATUS status = NtQuerySysInfo(
        (SYSTEM_INFORMATION_CLASS)SystemProcessorPerformanceInformation,
        perf_info,
        (ULONG)(sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * telemetry->active_core_count),
        &return_len
    );

    if (status != 0) return;

    uint64_t total_delta_idle = 0;
    uint64_t total_delta_system = 0;
    float max_temp = 0.0f;
    float max_voltage = 0.0f;

    for (uint32_t i = 0; i < telemetry->active_core_count; i++) {
        uint64_t raw_idle = (uint64_t)perf_info[i].IdleTime.QuadPart;
        uint64_t raw_kernel = (uint64_t)perf_info[i].KernelTime.QuadPart;
        uint64_t raw_user = (uint64_t)perf_info[i].UserTime.QuadPart;

        // İlk vuruşta (prev değerleri 0 ise) delta hesaplama
        if (telemetry->cores[i].prev_kernel_time == 0 && telemetry->cores[i].prev_user_time == 0) {
            telemetry->cores[i].prev_idle_time = raw_idle;
            telemetry->cores[i].prev_kernel_time = raw_kernel;
            telemetry->cores[i].prev_user_time = raw_user;
            continue;
        }

        uint64_t d_idle = raw_idle - telemetry->cores[i].prev_idle_time;
        uint64_t d_kernel = raw_kernel - telemetry->cores[i].prev_kernel_time;
        uint64_t d_user = raw_user - telemetry->cores[i].prev_user_time;
        uint64_t d_total = d_kernel + d_user;

        if (d_total > 0) {
            float usage = (1.0f - ((float)d_idle / (float)d_total)) * 100.0f;
            if (usage < 0.0f) usage = 0.0f;
            if (usage > 100.0f) usage = 100.0f;
            telemetry->cores[i].usage_pct = usage;
        } else {
            telemetry->cores[i].usage_pct = 0.0f;
        }

        total_delta_idle += d_idle;
        total_delta_system += d_total;

        telemetry->cores[i].prev_idle_time = raw_idle;
        telemetry->cores[i].prev_kernel_time = raw_kernel;
        telemetry->cores[i].prev_user_time = raw_user;

        if (read_msr) {
            uint64_t msr_19c = read_msr(i, MSR_IA32_THERM_STATUS);
            extract_core_temp(msr_19c, telemetry->tjmax, &telemetry->cores[i].temp_c);
            telemetry->cores[i].is_throttling = extract_throttling(msr_19c);
            if (telemetry->cores[i].temp_c > max_temp) max_temp = telemetry->cores[i].temp_c;

            uint64_t msr_198 = read_msr(i, MSR_IA32_PERF_STATUS);
            telemetry->cores[i].voltage_v = extract_voltage(msr_198);
            if (telemetry->cores[i].voltage_v > max_voltage) max_voltage = telemetry->cores[i].voltage_v;

            uint64_t cur_mperf = read_msr(i, MSR_IA32_MPERF);
            uint64_t cur_aperf = read_msr(i, MSR_IA32_APERF);

            uint64_t d_mperf = cur_mperf - telemetry->cores[i].prev_mperf;
            uint64_t d_aperf = cur_aperf - telemetry->cores[i].prev_aperf;

            if (d_mperf > 0 && telemetry->cores[i].prev_mperf > 0) {
                telemetry->cores[i].freq_mhz = (float)telemetry->base_bclk_mhz * ((float)d_aperf / (float)d_mperf) * 10.0f;
            }

            telemetry->cores[i].prev_mperf = cur_mperf;
            telemetry->cores[i].prev_aperf = cur_aperf;
        }
    }

    if (total_delta_system > 0) {
        float total_usage = (1.0f - ((float)total_delta_idle / (float)total_delta_system)) * 100.0f;
        if (total_usage < 0.0f) total_usage = 0.0f;
        if (total_usage > 100.0f) total_usage = 100.0f;
        telemetry->total_usage_pct = total_usage;
    }
    telemetry->package_temp_c = max_temp;
    telemetry->max_voltage_v = max_voltage;
}