#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "../include/telemetry.h"
#include <windows.h>

#define SystemPerformanceInformation 2

typedef LONG NTSTATUS;
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

// SDK cakismasini onlemek amaciyla ozel adlandirilmis tam performans yapisi
typedef struct _HW_SYSTEM_PERFORMANCE_INFO {
    LARGE_INTEGER IdleProcessTime;
    LARGE_INTEGER IoReadTransferCount;
    LARGE_INTEGER IoWriteTransferCount;
    LARGE_INTEGER IoOtherTransferCount;
    ULONG IoReadOperationCount;
    ULONG IoWriteOperationCount;
    ULONG IoOtherOperationCount;
    ULONG AvailablePages;
    ULONG CommittedPages;
    ULONG CommitLimit;
    ULONG PeakCommitment;
    ULONG PageFaultCount;
    ULONG CopyOnWriteCount;
    ULONG TransitionCount;
    ULONG CacheTransitionCount;
    ULONG DemandZeroCount;
    ULONG PageReadCount;
    ULONG PageReadIosCount;
    ULONG CacheReadCount;
    ULONG CacheIosCount;
    ULONG PagefilePagesWritten;
    ULONG PagefilePageWriteIosCount;
    ULONG PagedPoolUsage;
    ULONG NonPagedPoolUsage;
    ULONG PagedPoolAllocs;
    ULONG PagedPoolFrees;
    ULONG NonPagedPoolAllocs;
    ULONG NonPagedPoolFrees;
    ULONG TotalFreeSystemPtes;
    ULONG SystemCodePage;
    ULONG TotalSystemDriverPages;
    ULONG TotalSystemCodePages;
    ULONG SmallNonPagedLookasideListAllocateHits;
    ULONG SmallPagedLookasideListAllocateHits;
    ULONG Reserved3;
    ULONG MmSystemCachePage;
    ULONG PagedPoolPage;
    ULONG SystemDriverPage;
    ULONG FastReadNoWait;
    ULONG FastReadWait;
    ULONG FastReadResourceMiss;
    ULONG FastReadNotPossible;
    ULONG FastMdlReadNoWait;
    ULONG FastMdlReadWait;
    ULONG FastMdlReadResourceMiss;
    ULONG FastMdlReadNotPossible;
    ULONG MapDataNoWait;
    ULONG MapDataWait;
    ULONG MapDataWaitMiss;
    ULONG PinMappedDataCount;
    ULONG PinReadNoWait;
    ULONG PinReadWait;
    ULONG PinReadNoWaitMiss;
    ULONG PinReadWaitMiss;
    ULONG CopyReadNoWait;
    ULONG CopyReadWait;
    ULONG CopyReadNoWaitMiss;
    ULONG CopyReadWaitMiss;
    ULONG MdlReadNoWait;
    ULONG MdlReadWait;
    ULONG MdlReadNoWaitMiss;
    ULONG MdlReadWaitMiss;
    ULONG ReadAheadIosCount;
    ULONG MixedReadCount;
    ULONG LoadedModulePages;
    ULONG SystemLargePageUsage;
    ULONG PagedPoolCommit;
    ULONG NonPagedPoolCommit;
    ULONG ResidentSystemCodePage;
    ULONG TotalSystemLargePages;
    ULONG ProcessLargePageAllocations;
    ULONG Spare[2];
} HW_SYSTEM_PERFORMANCE_INFO;

typedef NTSTATUS (NTAPI *pfnNtQuerySystemInformation)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

static pfnNtQuerySystemInformation pNtQuerySysInfo = NULL;
static bool ntdll_initialized = false;
static uint64_t system_page_size = 4096;

static void init_nt_helpers(void) {
    if (ntdll_initialized) return;

    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    if (sys_info.dwPageSize > 0) {
        system_page_size = sys_info.dwPageSize;
    }

    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) hNtdll = LoadLibraryA("ntdll.dll");
    if (hNtdll) {
        pNtQuerySysInfo = (pfnNtQuerySystemInformation)GetProcAddress(hNtdll, "NtQuerySystemInformation");
    }
    ntdll_initialized = true;
}

int get_ram_status(RAMStatus *status) {
    if (!status) return 0;
    memset(status, 0, sizeof(RAMStatus));

    init_nt_helpers();

    // 1. Standart Fiziksel Bellek Telemetrisi
    MEMORYSTATUSEX mem_stat;
    mem_stat.dwLength = sizeof(MEMORYSTATUSEX);
    if (!GlobalMemoryStatusEx(&mem_stat)) {
        return 0;
    }

    status->memory_load    = mem_stat.dwMemoryLoad;
    status->total_phys_ram = mem_stat.ullTotalPhys;
    status->avail_phys_ram = mem_stat.ullAvailPhys;
    status->used_phys_ram  = mem_stat.ullTotalPhys - mem_stat.ullAvailPhys;

    // 2. BIOS / Donanima Rezerve Edilmis Bellek Tespiti
    ULONGLONG installed_ram_kb = 0;
    if (GetPhysicallyInstalledSystemMemory(&installed_ram_kb)) {
        status->installed_hardware_ram = (uint64_t)installed_ram_kb * 1024ULL;
        if (status->installed_hardware_ram > status->total_phys_ram) {
            status->hardware_reserved_bytes = status->installed_hardware_ram - status->total_phys_ram;
        } else {
            status->hardware_reserved_bytes = 0;
        }
    } else {
        status->installed_hardware_ram = status->total_phys_ram;
        status->hardware_reserved_bytes = 0;
    }

    // 3. NT Performance Havuzlari ve Commit Takibi
    if (pNtQuerySysInfo) {
        HW_SYSTEM_PERFORMANCE_INFO perf_info;
        ULONG return_len = 0;
        NTSTATUS nt_status = pNtQuerySysInfo(
            (ULONG)SystemPerformanceInformation,
            &perf_info,
            sizeof(HW_SYSTEM_PERFORMANCE_INFO),
            &return_len
        );

        if (NT_SUCCESS(nt_status)) {
            status->paged_pool_bytes     = (uint64_t)perf_info.PagedPoolUsage * system_page_size;
            status->non_paged_pool_bytes = (uint64_t)perf_info.NonPagedPoolUsage * system_page_size;
            status->commit_total_bytes   = (uint64_t)perf_info.CommittedPages * system_page_size;
            status->commit_limit_bytes   = (uint64_t)perf_info.CommitLimit * system_page_size;
            status->commit_peak_bytes    = (uint64_t)perf_info.PeakCommitment * system_page_size;
        }
    }

    return 1;
}