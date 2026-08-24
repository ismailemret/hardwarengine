#include "../include/telemetry.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (STDAPICALLTYPE *pawnio_open_fn)(PHANDLE handle);
typedef HRESULT (STDAPICALLTYPE *pawnio_load_fn)(HANDLE handle, const UCHAR *blob, SIZE_T size);
typedef HRESULT (STDAPICALLTYPE *pawnio_execute_fn)(HANDLE handle, PCSTR name,
                                                    const ULONG64 *in, SIZE_T in_size,
                                                    PULONG64 out, SIZE_T out_size,
                                                    PSIZE_T return_size);
typedef HRESULT (STDAPICALLTYPE *pawnio_close_fn)(HANDLE handle);

static HMODULE hPawnIOLib = NULL;
static HANDLE hPawnIO = NULL;
static pawnio_execute_fn pawnio_execute = NULL;
static pawnio_close_fn pawnio_close = NULL;

static bool load_file(const char *path, UCHAR **data, SIZE_T *size) {
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER file_size;
    bool ok = GetFileSizeEx(file, &file_size) && file_size.QuadPart > 0 &&
              (ULONGLONG)file_size.QuadPart <= SIZE_MAX;
    if (!ok) {
        CloseHandle(file);
        return false;
    }

    UCHAR *buffer = (UCHAR *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)file_size.QuadPart);
    DWORD bytes_read = 0;
    ok = buffer != NULL && ReadFile(file, buffer, (DWORD)file_size.QuadPart,
                                    &bytes_read, NULL) &&
         bytes_read == (DWORD)file_size.QuadPart;
    CloseHandle(file);

    if (!ok) {
        if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
        return false;
    }
    *data = buffer;
    *size = (SIZE_T)file_size.QuadPart;
    return true;
}

static bool get_application_path(char *path, DWORD path_size) {
    DWORD length = GetModuleFileNameA(NULL, path, path_size);
    if (!length || length >= path_size) return false;
    char *slash = strrchr(path, '\\');
    if (slash) slash[1] = '\0';
    else path[0] = '\0';
    return true;
}

bool init_kernel_driver(CpuVendor vendor) {
    char app_path[MAX_PATH];
    char module_path[MAX_PATH];
    UCHAR *module = NULL;
    SIZE_T module_size = 0;
    HANDLE pawnio_handle = NULL;
    char pawnio_path[MAX_PATH] = "C:\\Program Files\\PawnIO\\PawnIOLib.dll";

    if (vendor != CPU_VENDOR_INTEL && vendor != CPU_VENDOR_AMD) return false;
    if (!get_application_path(app_path, sizeof(app_path))) return false;
    _snprintf_s(module_path, sizeof(module_path), _TRUNCATE, "%s%s",
                app_path, vendor == CPU_VENDOR_INTEL ? "IntelMSR.bin" : "AMDFamily17.bin");

    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        char install_path[MAX_PATH];
        DWORD type = REG_SZ;
        DWORD size = sizeof(install_path);
        RegQueryValueExA(key, "InstallLocation", NULL, &type,
                 (LPBYTE)install_path, &size);
        RegCloseKey(key);
        if (size > 1 && install_path[0]) {
            _snprintf_s(pawnio_path, sizeof(pawnio_path), _TRUNCATE,
                        "%s%s", install_path,
                        install_path[strlen(install_path) - 1] == '\\' ?
                        "PawnIOLib.dll" : "\\PawnIOLib.dll");
        }
    }

    hPawnIOLib = LoadLibraryA(pawnio_path);
    if (!hPawnIOLib) return false;

    pawnio_open_fn pawnio_open = (pawnio_open_fn)GetProcAddress(hPawnIOLib, "pawnio_open");
    pawnio_load_fn pawnio_load = (pawnio_load_fn)GetProcAddress(hPawnIOLib, "pawnio_load");
    pawnio_execute = (pawnio_execute_fn)GetProcAddress(hPawnIOLib, "pawnio_execute");
    pawnio_close = (pawnio_close_fn)GetProcAddress(hPawnIOLib, "pawnio_close");
    if (!pawnio_open || !pawnio_load || !pawnio_execute || !pawnio_close ||
        FAILED(pawnio_open(&pawnio_handle)) || !load_file(module_path, &module, &module_size) ||
        FAILED(pawnio_load(pawnio_handle, module, module_size))) {
        if (module) HeapFree(GetProcessHeap(), 0, module);
        if (pawnio_handle && pawnio_close) pawnio_close(pawnio_handle);
        FreeLibrary(hPawnIOLib);
        hPawnIOLib = NULL;
        return false;
    }

    HeapFree(GetProcessHeap(), 0, module);
    hPawnIO = pawnio_handle;
    return true;
}

void close_kernel_driver(void) {
    if (hPawnIO && pawnio_close) {
        pawnio_close(hPawnIO);
        hPawnIO = NULL;
    }
    if (hPawnIOLib) {
        FreeLibrary(hPawnIOLib);
        hPawnIOLib = NULL;
    }
}

uint64_t read_msr_driver(uint32_t core_id, uint32_t msr_addr) {
    (void)core_id;
    if (!hPawnIO || !pawnio_execute) return 0;

    DWORD_PTR mask = (DWORD_PTR)1 << core_id;
    DWORD_PTR old_mask = SetThreadAffinityMask(GetCurrentThread(), mask);
    ULONG64 input = msr_addr;
    ULONG64 output = 0;
    SIZE_T return_size = 0;
    HRESULT status = pawnio_execute(hPawnIO, "ioctl_read_msr", &input, 1,
                                    &output, 1, &return_size);
    if (old_mask) SetThreadAffinityMask(GetCurrentThread(), old_mask);
    if (FAILED(status) || return_size != 1) return 0;
    return output;
}