#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "../include/telemetry.h"
#include <windows.h>
#include <winternl.h>
#include <intrin.h>
#include <stdlib.h>
#include <string.h>

#define SystemProcessorPerformanceInformation 8

typedef NTSTATUS(NTAPI *pfnNtQuerySystemInformation)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

// Intel Architected MSR Definitions
#define MSR_INTEL_MPERF              0x000000E7
#define MSR_INTEL_APERF              0x000000E8
#define MSR_INTEL_PERF_STATUS        0x00000198
#define MSR_INTEL_THERM_STATUS       0x0000019C
#define MSR_INTEL_TEMPERATURE_TARGET 0x000001A2

// AMD Zen Architected MSR Definitions
#define MSR_AMD_MPERF                0x000000E7
#define MSR_AMD_APERF                0x000000E8
#define MSR_AMD_PSTATE_0             0xC0010064
#define MSR_AMD_HARDWARE_THERMAL     0xC0010293

static pfnNtQuerySystemInformation NtQuerySysInfo = NULL;

CpuVendor detect_cpu_vendor(void) {
    int cpu_info[4];
    __cpuid(cpu_info, 0);

    char vendor_str[13];
    *(int*)(vendor_str)     = cpu_info[1]; // EBX
    *(int*)(vendor_str + 4) = cpu_info[3]; // EDX
    *(int*)(vendor_str + 8) = cpu_info[2]; // ECX
    vendor_str[12] = '\0';

    if (strcmp(vendor_str, "GenuineIntel") == 0) return CPU_VENDOR_INTEL;
    if (strcmp(vendor_str, "AuthenticAMD") == 0) return CPU_VENDOR_AMD;
    return CPU_VENDOR_UNKNOWN;
}

static void read_cpu_brand_string(char *brand_out) {
    int cpu_info[4];
    __cpuid(cpu_info, 0x80000000);
    uint32_t max_ext = (uint32_t)cpu_info[0];

    if (max_ext >= 0x80000004) {
        __cpuid((int*)(brand_out),      0x80000002);
        __cpuid((int*)(brand_out + 16), 0x80000003);
        __cpuid((int*)(brand_out + 32), 0x80000004);
        brand_out[48] = '\0';
    } else {
        strcpy_s(brand_out, 49, "Generic x86_64 Processor");
    }
}

int init_cpu_sensor(CpuTelemetry *telemetry, msr_reader_cb read_msr) {
    if (!telemetry) return 0;
    memset(telemetry, 0, sizeof(CpuTelemetry));

    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) hNtdll = LoadLibraryA("ntdll.dll");
    if (!hNtdll) return 0;

    NtQuerySysInfo = (pfnNtQuerySystemInformation)GetProcAddress(hNtdll, "NtQuerySystemInformation");
    if (!NtQuerySysInfo) return 0;

    telemetry->vendor = detect_cpu_vendor();
    read_cpu_brand_string(telemetry->brand_string);
    telemetry->base_bclk_mhz = 100;

    // Dynamic Topology Enumeration via PROCESSOR_RELATIONSHIP
    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &length);
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)malloc(length);

    if (buffer && GetLogicalProcessorInformationEx(RelationProcessorCore, buffer, &length)) {
        uint8_t *ptr = (uint8_t*)buffer;
        uint32_t logical_idx = 0;
        uint32_t physical_idx = 0;

        while (ptr < (uint8_t*)buffer + length) {
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)ptr;
            if (info->Relationship == RelationProcessorCore) {
                PROCESSOR_RELATIONSHIP *core = &info->Processor;
                CoreType core_type = CORE_TYPE_PERFORMANCE;

                // Intel Heterogeneous Core Differentiation
                if (telemetry->vendor == CPU_VENDOR_INTEL) {
                    if (core->EfficiencyClass == 0 && !(core->Flags & LTP_PC_SMT)) {
                        core_type = CORE_TYPE_EFFICIENCY;
                    }
                }

                for (WORD g = 0; g < core->GroupCount; ++g) {
                    KAFFINITY mask = core->GroupMask[g].Mask;
                    for (BYTE b = 0; b < sizeof(KAFFINITY) * 8; ++b) {
                        if ((mask >> b) & 1) {
                            if (logical_idx < MAX_LOGICAL_CORES) {
                                telemetry->cores[logical_idx].core_id = logical_idx;
                                telemetry->cores[logical_idx].physical_id = physical_idx;
                                telemetry->cores[logical_idx].type = core_type;
                                logical_idx++;
                            }
                        }
                    }
                }
                physical_idx++;
            }
            ptr += info->Size;
        }
        telemetry->active_core_count = (logical_idx > MAX_LOGICAL_CORES) ? MAX_LOGICAL_CORES : logical_idx;
        telemetry->physical_core_count = physical_idx;
        free(buffer);
    } else {
        if (buffer) free(buffer);
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        telemetry->active_core_count = (sys_info.dwNumberOfProcessors > MAX_LOGICAL_CORES)
                                       ? MAX_LOGICAL_CORES
                                       : sys_info.dwNumberOfProcessors;
        telemetry->physical_core_count = telemetry->active_core_count;
        for (uint32_t i = 0; i < telemetry->active_core_count; i++) {
            telemetry->cores[i].core_id = i;
            telemetry->cores[i].physical_id = i;
            telemetry->cores[i].type = CORE_TYPE_PERFORMANCE;
        }
    }

    // Hardware TjMax Resolution
    if (telemetry->vendor == CPU_VENDOR_INTEL && read_msr) {
        uint64_t msr_1a2_val = read_msr(0, MSR_INTEL_TEMPERATURE_TARGET);
        telemetry->tjmax = (uint32_t)((msr_1a2_val >> 16) & 0xFF);
        if (telemetry->tjmax == 0) telemetry->tjmax = 100;
    } else {
        telemetry->tjmax = 95; // Default AMD Zen Tctl/Tdie reference ceiling
    }

    return 1;
}

static inline void read_intel_core_msr(CpuTelemetry *telemetry, uint32_t i, msr_reader_cb read_msr) {
    CoreTelemetry *core = &telemetry->cores[i];

    // Thermal Delta (IA32_THERM_STATUS)
    uint64_t msr_19c = read_msr(i, MSR_INTEL_THERM_STATUS);
    if ((msr_19c >> 31) & 0x1) {
        uint32_t delta = (uint32_t)((msr_19c >> 16) & 0x7F);
        core->temp_c = (float)(telemetry->tjmax - delta);
        core->is_throttling = (bool)((msr_19c >> 2) & 0x1);
    }

    // Voltage (IA32_PERF_STATUS)
    uint64_t msr_198 = read_msr(i, MSR_INTEL_PERF_STATUS);
    uint32_t vid = (uint32_t)((msr_198 >> 32) & 0xFFFF);
    if (vid == 0) vid = (uint32_t)(msr_198 & 0xFFFF);
    core->voltage_v = (float)vid / 8192.0f;

    // Prefer the requested/current ratio so idle C-states do not pull the displayed clock down.
    uint32_t target_ratio = (uint32_t)((msr_198 >> 8) & 0xFF);
    if (target_ratio > 0) {
        core->freq_mhz = (float)telemetry->base_bclk_mhz * (float)target_ratio;
    }

    // Fall back to effective clock when IA32_PERF_STATUS does not expose a ratio.
    uint64_t cur_mperf = read_msr(i, MSR_INTEL_MPERF);
    uint64_t cur_aperf = read_msr(i, MSR_INTEL_APERF);

    if (target_ratio == 0 && core->prev_mperf > 0 && cur_mperf > core->prev_mperf) {
        uint64_t d_mperf = cur_mperf - core->prev_mperf;
        uint64_t d_aperf = cur_aperf - core->prev_aperf;
        if (d_mperf > 0) {
            core->freq_mhz = (float)telemetry->base_bclk_mhz * ((float)d_aperf / (float)d_mperf) * 10.0f;
        }
    }
    core->prev_mperf = cur_mperf;
    core->prev_aperf = cur_aperf;
}

static inline void read_amd_core_msr(CpuTelemetry *telemetry, uint32_t i, msr_reader_cb read_msr) {
    CoreTelemetry *core = &telemetry->cores[i];

    // Multiplier & Voltage (P-State 0 MSR 0xC0010064)
    uint64_t pstate = read_msr(i, MSR_AMD_PSTATE_0);
    uint32_t cpu_fid = (uint32_t)(pstate & 0xFF);
    uint32_t cpu_did = (uint32_t)((pstate >> 8) & 0x3F);
    if (cpu_did > 0) {
        core->freq_mhz = (200.0f * (float)cpu_fid) / (float)cpu_did;
    }

    uint32_t cpu_vid = (uint32_t)((pstate >> 14) & 0xFF);
    core->voltage_v = 1.550f - ((float)cpu_vid * 0.00625f);

    // APERF / MPERF Dynamic Frequency Delta
    uint64_t cur_mperf = read_msr(i, MSR_AMD_MPERF);
    uint64_t cur_aperf = read_msr(i, MSR_AMD_APERF);
    if (core->prev_mperf > 0 && cur_mperf > core->prev_mperf) {
        uint64_t d_mperf = cur_mperf - core->prev_mperf;
        uint64_t d_aperf = cur_aperf - core->prev_aperf;
        if (d_mperf > 0) {
            core->freq_mhz = (float)telemetry->base_bclk_mhz * ((float)d_aperf / (float)d_mperf) * 10.0f;
        }
    }
    core->prev_mperf = cur_mperf;
    core->prev_aperf = cur_aperf;

    // Thermal State Fallback (Tctl Register 0xC0010293)
    uint64_t therm = read_msr(i, MSR_AMD_HARDWARE_THERMAL);
    if (therm != 0) {
        core->temp_c = (float)((therm >> 21) & 0x7FF) * 0.125f;
    }
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
            if (telemetry->vendor == CPU_VENDOR_INTEL) {
                read_intel_core_msr(telemetry, i, read_msr);
            } else if (telemetry->vendor == CPU_VENDOR_AMD) {
                read_amd_core_msr(telemetry, i, read_msr);
            }

            if (telemetry->cores[i].temp_c > max_temp) max_temp = telemetry->cores[i].temp_c;
            if (telemetry->cores[i].voltage_v > max_voltage) max_voltage = telemetry->cores[i].voltage_v;
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