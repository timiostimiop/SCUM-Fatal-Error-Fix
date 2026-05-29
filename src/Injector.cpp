#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>

namespace {

std::wstring lower(std::wstring s) {
    for (wchar_t &c : s) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return s;
}

bool same_name(const wchar_t *a, const wchar_t *b) {
    if (!a || !b) {
        return false;
    }
    return lower(a) == lower(b);
}

bool file_exists(const std::wstring &path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring full_path(const std::wstring &path) {
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, buf, nullptr);
    if (len == 0 || len >= MAX_PATH) {
        return path;
    }
    return buf;
}

std::wstring file_name_from_path(const std::wstring &path) {
    std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::wstring next_to_me(const wchar_t *name) {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return name;
    }

    for (DWORD i = len; i > 0; --i) {
        if (path[i - 1] == L'\\' || path[i - 1] == L'/') {
            path[i] = L'\0';
            break;
        }
    }

    return std::wstring(path) + name;
}

DWORD find_pid(const wchar_t *exe_name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (same_name(pe.szExeFile, exe_name)) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return pid;
}

bool process_running(const wchar_t *exe_name) {
    return find_pid(exe_name) != 0;
}

bool battleye_running() {
    const wchar_t *names[] = {
        L"BEService.exe",
        L"BEService_x64.exe",
        L"BEDaisy.exe",
        L"SCUM_BE.exe"
    };

    for (const wchar_t *name : names) {
        if (process_running(name)) {
            std::wprintf(L"Refusing to inject while BattlEye process is running: %ls\n", name);
            return true;
        }
    }

    return false;
}

std::uintptr_t remote_module_base(DWORD pid, const wchar_t *module_name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);

    std::uintptr_t base = 0;
    if (Module32FirstW(snap, &me)) {
        do {
            if (same_name(me.szModule, module_name)) {
                base = reinterpret_cast<std::uintptr_t>(me.modBaseAddr);
                break;
            }
        } while (Module32NextW(snap, &me));
    }

    CloseHandle(snap);
    return base;
}

bool module_loaded(DWORD pid, const std::wstring &module_name) {
    return remote_module_base(pid, module_name.c_str()) != 0;
}

void *remote_loadlibraryw(DWORD pid) {
    HMODULE local_kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!local_kernel32) {
        return nullptr;
    }

    FARPROC local_loadlibrary = GetProcAddress(local_kernel32, "LoadLibraryW");
    if (!local_loadlibrary) {
        return nullptr;
    }

    std::uintptr_t remote_kernel32 = remote_module_base(pid, L"kernel32.dll");
    if (!remote_kernel32) {
        return nullptr;
    }

    auto offset =
        reinterpret_cast<std::uintptr_t>(local_loadlibrary) -
        reinterpret_cast<std::uintptr_t>(local_kernel32);

    return reinterpret_cast<void *>(remote_kernel32 + offset);
}

bool inject(DWORD pid, const std::wstring &dll_path) {
    if (module_loaded(pid, file_name_from_path(dll_path))) {
        std::wprintf(L"Already loaded: %ls\n", dll_path.c_str());
        return true;
    }

    void *load_library = remote_loadlibraryw(pid);
    if (!load_library) {
        std::printf("Could not resolve remote LoadLibraryW\n");
        return false;
    }

    HANDLE proc = OpenProcess(
        PROCESS_CREATE_THREAD |
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE |
        PROCESS_VM_READ,
        FALSE,
        pid);
    if (!proc) {
        std::printf("OpenProcess failed, gle=%lu\n", GetLastError());
        return false;
    }

    SIZE_T bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    void *remote_path = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) {
        std::printf("VirtualAllocEx failed, gle=%lu\n", GetLastError());
        CloseHandle(proc);
        return false;
    }

    SIZE_T written = 0;
    BOOL wrote = WriteProcessMemory(proc, remote_path, dll_path.c_str(), bytes, &written);
    if (!wrote || written != bytes) {
        std::printf("WriteProcessMemory failed, gle=%lu\n", GetLastError());
        VirtualFreeEx(proc, remote_path, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }

    HANDLE thread = CreateRemoteThread(
        proc,
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library),
        remote_path,
        0,
        nullptr);
    if (!thread) {
        std::printf("CreateRemoteThread failed, gle=%lu\n", GetLastError());
        VirtualFreeEx(proc, remote_path, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }

    WaitForSingleObject(thread, 15000);
    CloseHandle(thread);
    VirtualFreeEx(proc, remote_path, 0, MEM_RELEASE);
    CloseHandle(proc);

    std::wstring dll_name = file_name_from_path(dll_path);
    for (int i = 0; i < 20; ++i) {
        if (module_loaded(pid, dll_name)) {
            return true;
        }
        Sleep(250);
    }

    return false;
}

void usage() {
    std::printf(
        "ScumAudioDiagInjector\n"
        "\n"
        "Usage:\n"
        "  ScumAudioDiagInjector.exe [--dll path] [--timeout seconds] [--once]\n"
        "\n"
        "Defaults:\n"
        "  --dll      ScumAudioDiag.dll next to this injector\n"
        "  --timeout 0, wait forever\n"
        "\n"
        "Run SCUM.exe directly without BattlEye, then this tool injects the DLL and exits.\n");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    std::wstring dll_path = full_path(next_to_me(L"ScumAudioDiag.dll"));
    bool once = false;
    DWORD timeout_seconds = 0;

    for (int i = 1; i < argc; ++i) {
        if (same_name(argv[i], L"--help") || same_name(argv[i], L"-h")) {
            usage();
            return 0;
        }

        if (same_name(argv[i], L"--once")) {
            once = true;
            continue;
        }

        if (same_name(argv[i], L"--dll") && i + 1 < argc) {
            dll_path = full_path(argv[++i]);
            continue;
        }

        if (same_name(argv[i], L"--timeout") && i + 1 < argc) {
            timeout_seconds = wcstoul(argv[++i], nullptr, 10);
            continue;
        }

        std::wprintf(L"Unknown argument: %ls\n\n", argv[i]);
        usage();
        return 1;
    }

    if (!file_exists(dll_path)) {
        std::wprintf(L"DLL not found: %ls\n", dll_path.c_str());
        return 1;
    }

    std::wprintf(L"DLL: %ls\n", dll_path.c_str());
    std::printf("Waiting for SCUM.exe. Start the game by running SCUM.exe directly, not the BattlEye launcher.\n");

    DWORD start = GetTickCount();
    for (;;) {
        if (battleye_running()) {
            std::printf("Stop BattlEye and launch SCUM.exe directly before using this injector.\n");
            return 2;
        }

        DWORD pid = find_pid(L"SCUM.exe");
        if (pid) {
            std::printf("Found SCUM.exe pid=%lu\n", pid);
            bool ok = inject(pid, dll_path);
            std::printf(ok ? "Injected.\n" : "Injection failed.\n");
            return ok ? 0 : 3;
        }

        if (once) {
            std::printf("SCUM.exe is not running.\n");
            return 4;
        }

        if (timeout_seconds != 0 && (GetTickCount() - start) >= timeout_seconds * 1000UL) {
            std::printf("Timed out waiting for SCUM.exe.\n");
            return 5;
        }

        Sleep(1000);
    }
}
