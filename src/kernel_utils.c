#include "../include/telemetry.h"
#include <windows.h>
#include <stdio.h>

#define OLS_TYPE 40000  
#define IOCTL_OLS_READ_MSR CTL_CODE(OLS_TYPE, 0x821, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DRIVER_DEVICE_NAME "\\\\.\\WinRing0_1_2_0"

static HANDLE hDriver = INVALID_HANDLE_VALUE;

bool init_kernel_driver(void) {
    hDriver = CreateFileA(
        DRIVER_DEVICE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDriver == INVALID_HANDLE_VALUE) {
        return false;
    }
    return true;
}

void close_kernel_driver(void) {
    if (hDriver != INVALID_HANDLE_VALUE) {
        CloseHandle(hDriver);
        hDriver = INVALID_HANDLE_VALUE;
    }
}

// Belirli bir çekirdeğe bağlanıp MSR okuyan fonksiyon
uint64_t read_msr_driver(uint32_t core_id, uint32_t msr_addr) {
    if (hDriver == INVALID_HANDLE_VALUE) return 0;

    //Thread'i hedef çekirdeğe kilitle
    DWORD_PTR mask = (DWORD_PTR)1 << core_id;
    DWORD_PTR old_mask = SetThreadAffinityMask(GetCurrentThread(), mask);

    //IOCTL ile MSR Değerini İste
    uint32_t input_buffer = msr_addr;
    uint32_t output_buffer[2] = {0}; // [0] = EAX (Low), [1] = EDX (High)
    DWORD bytes_returned = 0;

    BOOL success = DeviceIoControl(
        hDriver,
        IOCTL_OLS_READ_MSR,
        &input_buffer,
        sizeof(input_buffer),
        output_buffer,
        sizeof(output_buffer),
        &bytes_returned,
        NULL
    );

    //Thread maskesini eski haline getir
    SetThreadAffinityMask(GetCurrentThread(), old_mask);

    if (!success) return 0;

    //64-bit MSR değerini birleştir: (EDX << 32) | EAX
    return ((uint64_t)output_buffer[1] << 32) | output_buffer[0];
}
