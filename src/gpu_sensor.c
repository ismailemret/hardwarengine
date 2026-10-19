#include "../include/telemetry.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// Opaque NVML handle and return types.
typedef void* nvmlDevice_t;
typedef int nvmlReturn_t;

// NVML status codes.
#define NVML_SUCCESS 0
#define NVML_ERROR_UNINITIALIZED 1
#define NVML_ERROR_INVALID_ARGUMENT 2
#define NVML_ERROR_NOT_SUPPORTED 3
#define NVML_ERROR_NO_PERMISSION 4
#define NVML_ERROR_NOT_FOUND 6
#define NVML_ERROR_DRIVER_NOT_LOADED 9
#define NVML_ERROR_GPU_IS_LOST 15

// NVML sensor and clock constants.
#define NVML_TEMPERATURE_GPU 0
#define NVML_CLOCK_GRAPHICS 0
#define NVML_CLOCK_SM 1
#define NVML_CLOCK_MEM 2

// Data layouts returned by NVML metric queries.
typedef struct {
    uint32_t gpu;
    uint32_t memory;
} nvmlUtilization_t;

typedef struct {
    uint64_t total;
    uint64_t free;
    uint64_t used;
} nvmlMemory_t;

// Dynamic function pointers resolved from nvml.dll.
typedef nvmlReturn_t (*pfn_nvmlInit_v2)(void);
typedef nvmlReturn_t (*pfn_nvmlShutdown)(void);
typedef nvmlReturn_t (*pfn_nvmlDeviceGetCount_v2)(unsigned int *deviceCount);
typedef nvmlReturn_t (*pfn_nvmlDeviceGetHandleByIndex_v2)(unsigned int index, nvmlDevice_t *device);
typedef nvmlReturn_t (*pfn_nvmlDeviceGetName)(nvmlDevice_t device, char *name, unsigned int length);
typedef nvmlReturn_t (*pfn_nvmlDeviceGetUtilizationRates)(nvmlDevice_t device, nvmlUtilization_t *utilization);
typedef nvmlReturn_t (*pfn_nvmlDeviceGetMemoryInfo)(nvmlDevice_t device, nvmlMemory_t *memory);
typedef nvmlReturn_t (*pfn_nvmlDeviceGetTemperature)(nvmlDevice_t device, int sensorType, unsigned int *temp);
typedef nvmlReturn_t (*pfn_nvmlDeviceGetPowerUsage)(nvmlDevice_t device, unsigned int *power);
typedef nvmlReturn_t (*pfn_nvmlDeviceGetClockInfo)(nvmlDevice_t device, int type, unsigned int *clock);
typedef nvmlReturn_t (*pfn_nvmlDeviceGetFanSpeed)(nvmlDevice_t device, unsigned int *speed);

// Runtime NVML module state and entry points.
static HMODULE hNvmlDll = NULL;
static pfn_nvmlInit_v2 p_nvmlInit = NULL;
static pfn_nvmlShutdown p_nvmlShutdown = NULL;
static pfn_nvmlDeviceGetCount_v2 p_nvmlDeviceGetCount = NULL;
static pfn_nvmlDeviceGetHandleByIndex_v2 p_nvmlDeviceGetHandleByIndex = NULL;
static pfn_nvmlDeviceGetName p_nvmlDeviceGetName = NULL;
static pfn_nvmlDeviceGetUtilizationRates p_nvmlDeviceGetUtilizationRates = NULL;
static pfn_nvmlDeviceGetMemoryInfo p_nvmlDeviceGetMemoryInfo = NULL;
static pfn_nvmlDeviceGetTemperature p_nvmlDeviceGetTemperature = NULL;
static pfn_nvmlDeviceGetPowerUsage p_nvmlDeviceGetPowerUsage = NULL;
static pfn_nvmlDeviceGetClockInfo p_nvmlDeviceGetClockInfo = NULL;
static pfn_nvmlDeviceGetFanSpeed p_nvmlDeviceGetFanSpeed = NULL;

static nvmlDevice_t nvml_device_handles[MAX_GPUS];
static bool nvml_dll_loaded = false;

// Power-state machine for NVIDIA Optimus and hybrid setups.
typedef enum {
    NVML_STATE_UNINITIALIZED = 0,
    NVML_STATE_AWAKE,
    NVML_STATE_SLEEPING
} NvmlState;

static NvmlState current_nvml_state = NVML_STATE_UNINITIALIZED;
static uint32_t wake_poll_counter = 0;
#define WAKE_POLL_INTERVAL 20 // 20 * 100ms = 2.0s polling rate when in RTD3 sleep.

// Zero dynamic telemetry values to prevent stale/ghost metrics during D3Cold sleep.
static void zero_gpu_dynamic_metrics(GpuDeviceTelemetry *gpu) {
    gpu->is_active = false;
    gpu->core_util_pct = 0.0f;
    gpu->memory_util_pct = 0.0f;
    gpu->core_clock_mhz = 0;
    gpu->memory_clock_mhz = 0;
    gpu->temp_c = 0.0f;
    gpu->hotspot_temp_c = 0.0f;
    gpu->power_w = 0.0f;
    gpu->fan_speed_pct = 0;
    gpu->vram_used_bytes = 0;
    gpu->vram_free_bytes = 0;
}

// Identify whether the NVML status code indicates an unreachable or sleeping dGPU.
static inline bool is_nvml_sleep_or_lost_error(nvmlReturn_t ret) {
    return (ret == NVML_ERROR_DRIVER_NOT_LOADED ||
            ret == NVML_ERROR_GPU_IS_LOST ||
            ret == NVML_ERROR_UNINITIALIZED ||
            ret == NVML_ERROR_NOT_FOUND);
}

// Fallback search paths for runtime dynamic binding of NVML.
static bool load_nvml_library(void) {
    if (hNvmlDll) return true;

    static const char *const nvml_paths[] = {
        "nvml.dll",
        "C:\\Windows\\System32\\nvml.dll",
        "C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll"
    };

    for (size_t i = 0; i < sizeof(nvml_paths) / sizeof(nvml_paths[0]); ++i) {
        hNvmlDll = LoadLibraryA(nvml_paths[i]);
        if (hNvmlDll) break;
    }

    if (!hNvmlDll) return false;

    p_nvmlInit = (pfn_nvmlInit_v2)GetProcAddress(hNvmlDll, "nvmlInit_v2");
    if (!p_nvmlInit) p_nvmlInit = (pfn_nvmlInit_v2)GetProcAddress(hNvmlDll, "nvmlInit");

    p_nvmlShutdown = (pfn_nvmlShutdown)GetProcAddress(hNvmlDll, "nvmlShutdown");
    p_nvmlDeviceGetCount = (pfn_nvmlDeviceGetCount_v2)GetProcAddress(hNvmlDll, "nvmlDeviceGetCount_v2");
    p_nvmlDeviceGetHandleByIndex = (pfn_nvmlDeviceGetHandleByIndex_v2)GetProcAddress(hNvmlDll, "nvmlDeviceGetHandleByIndex_v2");

    if (!p_nvmlInit || !p_nvmlShutdown || !p_nvmlDeviceGetCount || !p_nvmlDeviceGetHandleByIndex) {
        FreeLibrary(hNvmlDll);
        hNvmlDll = NULL;
        return false;
    }

    p_nvmlDeviceGetName = (pfn_nvmlDeviceGetName)GetProcAddress(hNvmlDll, "nvmlDeviceGetName");
    p_nvmlDeviceGetUtilizationRates = (pfn_nvmlDeviceGetUtilizationRates)GetProcAddress(hNvmlDll, "nvmlDeviceGetUtilizationRates");
    p_nvmlDeviceGetMemoryInfo = (pfn_nvmlDeviceGetMemoryInfo)GetProcAddress(hNvmlDll, "nvmlDeviceGetMemoryInfo");
    p_nvmlDeviceGetTemperature = (pfn_nvmlDeviceGetTemperature)GetProcAddress(hNvmlDll, "nvmlDeviceGetTemperature");
    p_nvmlDeviceGetPowerUsage = (pfn_nvmlDeviceGetPowerUsage)GetProcAddress(hNvmlDll, "nvmlDeviceGetPowerUsage");
    p_nvmlDeviceGetClockInfo = (pfn_nvmlDeviceGetClockInfo)GetProcAddress(hNvmlDll, "nvmlDeviceGetClockInfo");
    p_nvmlDeviceGetFanSpeed = (pfn_nvmlDeviceGetFanSpeed)GetProcAddress(hNvmlDll, "nvmlDeviceGetFanSpeed");

    return true;
}

int init_gpu_sensor(GpuTelemetry *telemetry) {
    if (!telemetry) return 0;
    memset(telemetry, 0, sizeof(GpuTelemetry));

    if (!load_nvml_library()) {
        return 0;
    }

    nvml_dll_loaded = true;
    current_nvml_state = NVML_STATE_UNINITIALIZED;
    wake_poll_counter = WAKE_POLL_INTERVAL;

    return 1;
}

void update_gpu_telemetry(GpuTelemetry *telemetry) {
    if (!telemetry || !nvml_dll_loaded) return;

    // Handle initial detection and wake-up transitions from RTD3 sleep.
    if (current_nvml_state == NVML_STATE_UNINITIALIZED || current_nvml_state == NVML_STATE_SLEEPING) {
        wake_poll_counter++;

        if (wake_poll_counter >= WAKE_POLL_INTERVAL) {
            wake_poll_counter = 0;

            if (p_nvmlInit() == NVML_SUCCESS) {
                unsigned int device_count = 0;
                if (p_nvmlDeviceGetCount(&device_count) == NVML_SUCCESS && device_count > 0) {
                    telemetry->gpu_count = (device_count > MAX_GPUS) ? MAX_GPUS : device_count;

                    for (unsigned int i = 0; i < telemetry->gpu_count; i++) {
                        GpuDeviceTelemetry *gpu = &telemetry->gpus[i];
                        gpu->gpu_index = i;
                        gpu->vendor = GPU_VENDOR_NVIDIA;

                        if (p_nvmlDeviceGetHandleByIndex(i, &nvml_device_handles[i]) == NVML_SUCCESS) {
                            gpu->is_active = true;
                            if (p_nvmlDeviceGetName) {
                                p_nvmlDeviceGetName(nvml_device_handles[i], gpu->name, sizeof(gpu->name));
                            }
                        } else {
                            gpu->is_active = false;
                        }
                    }
                    current_nvml_state = NVML_STATE_AWAKE;
                } else {
                    p_nvmlShutdown();
                    current_nvml_state = NVML_STATE_SLEEPING;
                }
            } else {
                current_nvml_state = NVML_STATE_SLEEPING;
            }
        }

        if (current_nvml_state != NVML_STATE_AWAKE) {
            for (uint32_t i = 0; i < telemetry->gpu_count; i++) {
                zero_gpu_dynamic_metrics(&telemetry->gpus[i]);
            }
            return;
        }
    }

    bool gpu_went_to_sleep = false;

    // Collect runtime metrics across all detected active GPUs.
    for (uint32_t i = 0; i < telemetry->gpu_count; i++) {
        GpuDeviceTelemetry *gpu = &telemetry->gpus[i];
        if (!gpu->is_active) continue;

        nvmlDevice_t dev = nvml_device_handles[i];
        nvmlReturn_t ret;

        // Core and memory utilization.
        if (p_nvmlDeviceGetUtilizationRates) {
            nvmlUtilization_t util;
            ret = p_nvmlDeviceGetUtilizationRates(dev, &util);
            if (ret == NVML_SUCCESS) {
                gpu->core_util_pct = (float)util.gpu;
                gpu->memory_util_pct = (float)util.memory;
            } else if (is_nvml_sleep_or_lost_error(ret)) {
                gpu_went_to_sleep = true;
                break;
            }
        }

        // VRAM metrics.
        if (p_nvmlDeviceGetMemoryInfo) {
            nvmlMemory_t mem;
            ret = p_nvmlDeviceGetMemoryInfo(dev, &mem);
            if (ret == NVML_SUCCESS) {
                gpu->vram_total_bytes = mem.total;
                gpu->vram_used_bytes = mem.used;
                gpu->vram_free_bytes = mem.free;
            } else if (is_nvml_sleep_or_lost_error(ret)) {
                gpu_went_to_sleep = true;
                break;
            }
        }

        // Package thermal sensor.
        if (p_nvmlDeviceGetTemperature) {
            unsigned int temp = 0;
            ret = p_nvmlDeviceGetTemperature(dev, NVML_TEMPERATURE_GPU, &temp);
            if (ret == NVML_SUCCESS) {
                gpu->temp_c = (float)temp;
            } else if (is_nvml_sleep_or_lost_error(ret)) {
                gpu_went_to_sleep = true;
                break;
            }
        }

        // Power consumption (converted from milliwatts to watts).
        if (p_nvmlDeviceGetPowerUsage) {
            unsigned int power_mw = 0;
            ret = p_nvmlDeviceGetPowerUsage(dev, &power_mw);
            if (ret == NVML_SUCCESS) {
                gpu->power_w = (float)power_mw / 1000.0f;
            } else if (is_nvml_sleep_or_lost_error(ret)) {
                gpu_went_to_sleep = true;
                break;
            } else if (ret == NVML_ERROR_NOT_SUPPORTED) {
                gpu->power_w = 0.0f;
            }
        }

        // Fan speed percentage (handled gracefully on laptops/mobile GPUs).
        if (p_nvmlDeviceGetFanSpeed) {
            unsigned int fan_speed = 0;
            ret = p_nvmlDeviceGetFanSpeed(dev, &fan_speed);
            if (ret == NVML_SUCCESS) {
                gpu->fan_speed_pct = fan_speed;
            } else if (is_nvml_sleep_or_lost_error(ret)) {
                gpu_went_to_sleep = true;
                break;
            } else {
                // Not supported on mobile dGPUs controlled by embedded controllers.
                gpu->fan_speed_pct = 0;
            }
        }

        // Core and memory clock frequencies.
        if (p_nvmlDeviceGetClockInfo) {
            unsigned int core_clk = 0;
            unsigned int mem_clk = 0;

            ret = p_nvmlDeviceGetClockInfo(dev, NVML_CLOCK_GRAPHICS, &core_clk);
            if (ret == NVML_SUCCESS) {
                gpu->core_clock_mhz = core_clk;
            } else if (is_nvml_sleep_or_lost_error(ret)) {
                gpu_went_to_sleep = true;
                break;
            }

            ret = p_nvmlDeviceGetClockInfo(dev, NVML_CLOCK_MEM, &mem_clk);
            if (ret == NVML_SUCCESS) {
                gpu->memory_clock_mhz = mem_clk;
            } else if (is_nvml_sleep_or_lost_error(ret)) {
                gpu_went_to_sleep = true;
                break;
            }
        }
    }

    // Teardown context immediately if dGPU entered D3Cold or lost PCIe link.
    if (gpu_went_to_sleep) {
        p_nvmlShutdown();
        current_nvml_state = NVML_STATE_SLEEPING;
        wake_poll_counter = 0;

        for (uint32_t i = 0; i < telemetry->gpu_count; i++) {
            zero_gpu_dynamic_metrics(&telemetry->gpus[i]);
        }
    }
}

void shutdown_gpu_sensor(void) {
    if (nvml_dll_loaded) {
        if (current_nvml_state == NVML_STATE_AWAKE && p_nvmlShutdown) {
            p_nvmlShutdown();
        }
        current_nvml_state = NVML_STATE_UNINITIALIZED;
        nvml_dll_loaded = false;
    }

    if (hNvmlDll) {
        FreeLibrary(hNvmlDll);
        hNvmlDll = NULL;
    }
}