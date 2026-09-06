#ifndef TELEMETRY_H
#define TELEMETRY_H

#define HW_API

#include <windows.h>
#include <psapi.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_LOGICAL_CORES 64
#define MAX_GPUS 4

typedef uint64_t (*msr_reader_cb)(uint32_t core_id, uint32_t msr_addr);
// CPU vendor and core type enums
typedef enum {
    CPU_VENDOR_UNKNOWN = 0,
    CPU_VENDOR_INTEL,
    CPU_VENDOR_AMD
} CpuVendor;

typedef enum {
    CORE_TYPE_UNKNOWN = 0,
    CORE_TYPE_PERFORMANCE,
    CORE_TYPE_EFFICIENCY
} CoreType;
// RAM status structure
typedef struct {
    DWORD memory_load;
    uint64_t total_phys_ram;
    uint64_t avail_phys_ram;
    uint64_t used_phys_ram;
    uint64_t installed_hardware_ram;
    uint64_t hardware_reserved_bytes;
    uint64_t paged_pool_bytes;
    uint64_t non_paged_pool_bytes;
    uint64_t commit_total_bytes;
    uint64_t commit_limit_bytes;
    uint64_t commit_peak_bytes;
} RAMStatus;

HW_API int get_ram_status(RAMStatus *status);
// CPU telemetry structures and functions
typedef struct {
    uint32_t core_id;
    uint32_t physical_id;
    CoreType type;
    float usage_pct;
    uint64_t prev_idle_time;
    uint64_t prev_kernel_time;
    uint64_t prev_user_time;
    float temp_c;
    bool is_thermal_throttling;
    bool is_power_throttling;
    float freq_mhz;
    uint64_t prev_mperf;
    uint64_t prev_aperf;
    uint64_t prev_tsc;
    float voltage_v;
} CoreTelemetry;

typedef struct {
    CpuVendor vendor;
    char brand_string[49];
    float total_usage_pct;
    float package_temp_c;
    float package_power_w;
    float max_voltage_v;
    uint32_t tjmax;
    uint32_t base_bclk_mhz;
    uint32_t active_core_count;
    uint32_t physical_core_count;
    double energy_unit_joules;
    uint64_t prev_energy_raw;
    uint64_t prev_energy_time;
    CoreTelemetry cores[MAX_LOGICAL_CORES];
} CpuTelemetry;
// Function prototypes
HW_API int init_cpu_sensor(CpuTelemetry *telemetry, msr_reader_cb read_msr);
HW_API void update_cpu_telemetry(CpuTelemetry *telemetry, msr_reader_cb read_msr);
HW_API CpuVendor detect_cpu_vendor(void);
// GPU telemetry structures and functions
typedef enum {
    GPU_VENDOR_UNKNOWN = 0,
    GPU_VENDOR_NVIDIA,
    GPU_VENDOR_AMD,
    GPU_VENDOR_INTEL
} GpuVendor;

typedef struct {
    uint32_t gpu_index;
    GpuVendor vendor;
    char name[64];
    bool is_active;

    // Utilization and clocks
    float core_util_pct;
    float memory_util_pct;
    uint32_t core_clock_mhz;
    uint32_t memory_clock_mhz;

    // Thermal and power metrics
    float temp_c;
    float hotspot_temp_c;
    float power_w;
    uint32_t fan_speed_pct;

    // VRAM metrics (bytes)
    uint64_t vram_total_bytes;
    uint64_t vram_used_bytes;
    uint64_t vram_free_bytes;
} GpuDeviceTelemetry;

typedef struct {
    uint32_t gpu_count;
    GpuDeviceTelemetry gpus[MAX_GPUS];
} GpuTelemetry;

int init_gpu_sensor(GpuTelemetry *telemetry);
void update_gpu_telemetry(GpuTelemetry *telemetry);
void shutdown_gpu_sensor(void);

// Process self-metrics structure and functions
typedef struct {
    float cpu_usage_pct;
    uint64_t working_set_bytes;
    uint64_t private_bytes;
    uint64_t prev_process_time;
    uint64_t prev_wall_time;
} SelfMetrics;
// Function prototypes
HW_API void init_self_metrics(SelfMetrics *metrics);
HW_API void update_self_metrics(SelfMetrics *metrics, uint32_t logical_core_count);
// Kernel driver interaction functions
HW_API bool init_kernel_driver(CpuVendor vendor);
HW_API void close_kernel_driver(void);
HW_API uint64_t read_msr_driver(uint32_t core_id, uint32_t msr_addr);

#endif // TELEMETRY_H