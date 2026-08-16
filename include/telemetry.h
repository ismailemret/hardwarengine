#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <windows.h>

// ==================== RAM SENSOR ====================
typedef struct {
    DWORD memory_load;           // Yüzde (0-100)
    unsigned long long total_ram; // Toplam RAM (bytes)
    unsigned long long avail_ram; // Kullanılabilir RAM (bytes)
    unsigned long long used_ram;  // Kullanılan RAM (bytes)
} RAMStatus;

int get_ram_status(RAMStatus *status);

// ==================== CPU SENSOR ====================


// ==================== GPU SENSOR ====================


// ==================== KERNEL UTILS ====================


#endif
