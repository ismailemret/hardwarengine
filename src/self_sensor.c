#include "../include/telemetry.h"

static inline uint64_t filetime_to_u64(FILETIME ft) {
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

void init_self_metrics(SelfMetrics *metrics) {
    if (!metrics) return;
    memset(metrics, 0, sizeof(SelfMetrics));

    FILETIME creation_time, exit_time, kernel_time, user_time;
    GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time);

    FILETIME now;
    GetSystemTimeAsFileTime(&now);

    metrics->prev_process_time = filetime_to_u64(kernel_time) + filetime_to_u64(user_time);
    metrics->prev_wall_time    = filetime_to_u64(now);
}

void update_self_metrics(SelfMetrics *metrics, uint32_t logical_core_count) {
    if (!metrics || logical_core_count == 0) return;

    FILETIME creation_time, exit_time, kernel_time, user_time;
    GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time);

    FILETIME now;
    GetSystemTimeAsFileTime(&now);

    uint64_t current_process_time = filetime_to_u64(kernel_time) + filetime_to_u64(user_time);
    uint64_t current_wall_time    = filetime_to_u64(now);

    uint64_t delta_process = current_process_time - metrics->prev_process_time;
    uint64_t delta_wall    = current_wall_time - metrics->prev_wall_time;

    if (delta_wall > 0) {
        metrics->cpu_usage_pct = ((float)delta_process / (float)(delta_wall * logical_core_count)) * 100.0f;
    } else {
        metrics->cpu_usage_pct = 0.0f;
    }

    metrics->prev_process_time = current_process_time;
    metrics->prev_wall_time    = current_wall_time;

    // Read process memory counters via PSAPI
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        metrics->working_set_bytes = pmc.WorkingSetSize;
        metrics->private_bytes     = pmc.PrivateUsage;
    }
}