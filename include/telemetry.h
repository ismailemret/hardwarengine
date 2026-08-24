#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <windows.h>
#include <psapi.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_LOGICAL_CORES 64

// Kernel MSR callback signature
typedef uint64_t (*msr_reader_cb)(uint32_t core_id, uint32_t msr_addr);

// ==================== CPU TOPOLOGY & IDENTIFIERS ====================
typedef enum {
    CPU_VENDOR_UNKNOWN = 0,
    CPU_VENDOR_INTEL,
    CPU_VENDOR_AMD
} CpuVendor;

typedef enum {
    CORE_TYPE_UNKNOWN = 0,
    CORE_TYPE_PERFORMANCE, // Intel P-Core / AMD Unified Core
    CORE_TYPE_EFFICIENCY   // Intel E-Core (Gracemont/Crestmont)
} CoreType;

// ==================== RAM TELEMETRY ====================
typedef struct {
    DWORD memory_load;
    unsigned long long total_ram;
    unsigned long long avail_ram;
    unsigned long long used_ram;
} RAMStatus;

int get_ram_status(RAMStatus *status);

// ==================== CPU TELEMETRY ====================
typedef struct {
    uint32_t core_id;          // Logical core index (0 .. N-1)
    uint32_t physical_id;      // Physical package core ID
    CoreType type;             // Architectural classification

    // Ring 3 Execution Metric
    float usage_pct;
    uint64_t prev_idle_time;
    uint64_t prev_kernel_time;
    uint64_t prev_user_time;

    // Thermal State (Ring 0)
    float temp_c;
    bool is_throttling;

    // Effective Frequency (Ring 0: IA32_APERF 0xE8 / IA32_MPERF 0xE7)
    float freq_mhz;
    uint64_t prev_mperf;
    uint64_t prev_aperf;

    // SVID Voltage (Ring 0)
    float voltage_v;
} CoreTelemetry;

typedef struct {
    CpuVendor vendor;
    char brand_string[49];
    float total_usage_pct;
    float package_temp_c;
    float max_voltage_v;
    uint32_t tjmax;
    uint32_t base_bclk_mhz;
    uint32_t active_core_count;
    uint32_t physical_core_count;
    CoreTelemetry cores[MAX_LOGICAL_CORES];
} CpuTelemetry;

int init_cpu_sensor(CpuTelemetry *telemetry, msr_reader_cb read_msr);
void update_cpu_telemetry(CpuTelemetry *telemetry, msr_reader_cb read_msr);
CpuVendor detect_cpu_vendor(void);

// ==================== PROCESS SELF-PROFILING ====================
typedef struct {
    float cpu_usage_pct;
    uint64_t working_set_bytes; // Physical RAM in working set
    uint64_t private_bytes;     // Private committed memory
    uint64_t prev_process_time;
    uint64_t prev_wall_time;
} SelfMetrics;

void init_self_metrics(SelfMetrics *metrics);
void update_self_metrics(SelfMetrics *metrics, uint32_t logical_core_count);

// ==================== KERNEL UTILS ====================
bool init_kernel_driver(CpuVendor vendor);
void close_kernel_driver(void);
uint64_t read_msr_driver(uint32_t core_id, uint32_t msr_addr);

#endif // TELEMETRY_H