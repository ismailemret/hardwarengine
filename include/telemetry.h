#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_LOGICAL_CORES 64

// Callback Tür Tanımı
typedef uint64_t (*msr_reader_cb)(uint32_t core_id, uint32_t msr_addr);

// ==================== RAM SENSOR ====================
typedef struct {
    DWORD memory_load;            // Yüzde (0-100)
    unsigned long long total_ram; // Toplam RAM (bytes)
    unsigned long long avail_ram; // Kullanılabilir RAM (bytes)
    unsigned long long used_ram;  // Kullanılan RAM (bytes)
} RAMStatus;

int get_ram_status(RAMStatus *status);

// ==================== CPU SENSOR ====================
typedef struct {
    uint32_t core_id;
    
    // Ring 3 kullanım yüzdeleri
    float usage_pct;
    uint64_t prev_idle_time;
    uint64_t prev_kernel_time;
    uint64_t prev_user_time;

    // Sıcaklık & Termal Durum (MSR 0x19C - Ring 0)
    float temp_c;              // Anlık Sıcaklık (°C) = TjMax - Delta
    bool is_throttling;        // Thermal Throttling / PROCHOT aktif mi?

    // Frekans Sayaçları (MSR 0xE7 & 0xE8 - Ring 0)
    float freq_mhz;            // Anlık Gerçek Saat Hızı (MHz)
    uint64_t prev_mperf;       // Önceki MPERF sayacı (t1)
    uint64_t prev_aperf;       // Önceki APERF sayacı (t1)

    // Voltaj (MSR 0x198 - Ring 0)
    float voltage_v;           // Çekirdek SVID Voltajı (V)
} CoreTelemetry;

// CPU Genel Durum Paketi
typedef struct {
    float total_usage_pct;      // Ortalama sistem yükü (%)
    float package_temp_c;       // En sıcak çekirdek veya MSR 0x1B1 Package sıcaklığı
    float max_voltage_v;        // En yüksek anlık çekirdek voltajı
    uint32_t tjmax;             // MSR 0x1A2 - Donanımsal Tavan Sıcaklık (Sabit)
    uint32_t base_bclk_mhz;     // Taban saat hızı (Standart: 100.0 MHz)
    uint32_t active_core_count; // Sistemdeki aktif izlek sayısı
    CoreTelemetry cores[MAX_LOGICAL_CORES];
} CpuTelemetry;

int init_cpu_sensor(CpuTelemetry *telemetry, uint64_t msr_1a2_val);
void update_cpu_telemetry(CpuTelemetry *telemetry, msr_reader_cb read_msr);

// ==================== GPU SENSOR ====================


// ==================== KERNEL UTILS ====================
bool init_kernel_driver(void);
void close_kernel_driver(void);
uint64_t read_msr_driver(uint32_t core_id, uint32_t msr_addr);

#endif // TELEMETRY_H