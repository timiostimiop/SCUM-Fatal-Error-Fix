#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace {

constexpr std::uintptr_t kXaudioOpenRva = 0x4B16410;
constexpr std::uintptr_t kMenuHoverRva = 0x42DC090;

constexpr std::size_t kJmpLen = 12;
constexpr std::size_t kXaudioOpenLen = 12;
constexpr std::size_t kMenuHoverLen = 17;

constexpr unsigned char kXaudioOpenSig[kXaudioOpenLen] = {
    0x48, 0x89, 0x5C, 0x24, 0x18,
    0x55,
    0x56,
    0x57,
    0x41, 0x56,
    0x41, 0x57
};

constexpr unsigned char kMenuHoverSig[kMenuHoverLen] = {
    0x48, 0x89, 0x5C, 0x24, 0x08,
    0x57,
    0x48, 0x83, 0xEC, 0x20,
    0x44, 0x8B, 0x81, 0x24, 0x01, 0x00, 0x00
};

using XaudioOpenFn = unsigned char(__fastcall *)(void *self, void *params);
using MenuHoverFn = void *(__fastcall *)(void *source, void *out);

XaudioOpenFn xaudio_open_orig = nullptr;
MenuHoverFn menu_hover_orig = nullptr;
bool xaudio_hooked = false;
bool menu_hover_hooked = false;

bool same_bytes(const unsigned char *a, const unsigned char *b, std::size_t len) {
    if (!a || !b) {
        return false;
    }

    for (std::size_t i = 0; i < len; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }

    return true;
}

bool can_read(const void *ptr, std::size_t len = sizeof(void *)) {
    if (!ptr) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) {
        return false;
    }

    if (mbi.State != MEM_COMMIT) {
        return false;
    }

    if ((mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) {
        return false;
    }

    auto start = reinterpret_cast<std::uintptr_t>(ptr);
    auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    auto off = start - base;
    return off <= mbi.RegionSize && len <= (mbi.RegionSize - off);
}

bool can_write(const void *ptr, std::size_t len = sizeof(void *)) {
    if (!ptr) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) {
        return false;
    }

    if (mbi.State != MEM_COMMIT) {
        return false;
    }

    if ((mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) {
        return false;
    }

    constexpr DWORD writable =
        PAGE_READWRITE |
        PAGE_WRITECOPY |
        PAGE_EXECUTE_READWRITE |
        PAGE_EXECUTE_WRITECOPY;
    if ((mbi.Protect & writable) == 0) {
        return false;
    }

    auto start = reinterpret_cast<std::uintptr_t>(ptr);
    auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    auto off = start - base;
    return off <= mbi.RegionSize && len <= (mbi.RegionSize - off);
}

template <typename T>
bool read_mem(const void *addr, T *out) {
    if (!out || !can_read(addr, sizeof(T))) {
        return false;
    }

    __try {
        *out = *reinterpret_cast<const T *>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
void write_mem(void *addr, T value) {
    if (!can_write(addr, sizeof(T))) {
        return;
    }

    __try {
        *reinterpret_cast<T *>(addr) = value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool put_jmp(void *at, std::size_t len, const void *to) {
    if (len < kJmpLen || len > 64) {
        return false;
    }

    unsigned char patch[64] = {};
    for (std::size_t i = 0; i < len; ++i) {
        patch[i] = 0x90;
    }

    patch[0] = 0x48;
    patch[1] = 0xB8;
    *reinterpret_cast<std::uint64_t *>(&patch[2]) =
        reinterpret_cast<std::uint64_t>(to);
    patch[10] = 0xFF;
    patch[11] = 0xE0;

    DWORD old_protect = 0;
    if (!VirtualProtect(at, len, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    CopyMemory(at, patch, len);
    FlushInstructionCache(GetCurrentProcess(), at, len);

    DWORD unused = 0;
    VirtualProtect(at, len, old_protect, &unused);
    return true;
}

void *make_tramp(unsigned char *target, std::size_t stolen_len) {
    auto *tramp = static_cast<unsigned char *>(
        VirtualAlloc(nullptr, stolen_len + kJmpLen, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) {
        return nullptr;
    }

    CopyMemory(tramp, target, stolen_len);
    if (!put_jmp(tramp + stolen_len, kJmpLen, target + stolen_len)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return nullptr;
    }

    return tramp;
}

unsigned char __fastcall xaudio_open_hook(void *self, void *params) {
    if (!self) {
        return 0;
    }

    auto base = reinterpret_cast<std::uintptr_t>(self);

    std::uint32_t flags = 0;
    void *xaudio = nullptr;

    bool got_flags = read_mem(reinterpret_cast<void *>(base + 0x270), &flags);
    bool got_xaudio = read_mem(reinterpret_cast<void *>(base + 0x1F8), &xaudio);

    bool opening_stream = got_flags && ((flags & 1u) != 0) && ((flags & 2u) == 0);
    if (opening_stream && (!got_xaudio || !can_read(xaudio))) {
        return 0;
    }

    return xaudio_open_orig ? xaudio_open_orig(self, params) : 0;
}

void * __fastcall menu_hover_hook(void *source, void *out) {
    if (!source) {
        if (out && can_write(out, 0x18)) {
            write_mem<std::uint64_t>(reinterpret_cast<unsigned char *>(out) + 0x00, 0);
            write_mem<std::uint64_t>(reinterpret_cast<unsigned char *>(out) + 0x08, 0);
            write_mem<std::uint32_t>(reinterpret_cast<unsigned char *>(out) + 0x10, 0xFFFFFFFFu);
            write_mem<std::uint32_t>(reinterpret_cast<unsigned char *>(out) + 0x14, 0);
        }
        return out;
    }

    return menu_hover_orig ? menu_hover_orig(source, out) : out;
}

bool hook_xaudio(HMODULE exe) {
    auto *target = reinterpret_cast<unsigned char *>(
        reinterpret_cast<std::uintptr_t>(exe) + kXaudioOpenRva);

    if (!same_bytes(target, kXaudioOpenSig, kXaudioOpenLen)) {
        return false;
    }

    void *tramp = make_tramp(target, kXaudioOpenLen);
    if (!tramp) {
        return false;
    }

    xaudio_open_orig = reinterpret_cast<XaudioOpenFn>(tramp);
    if (!put_jmp(target, kXaudioOpenLen, reinterpret_cast<void *>(&xaudio_open_hook))) {
        return false;
    }

    xaudio_hooked = true;
    return true;
}

bool hook_menu_hover(HMODULE exe) {
    auto *target = reinterpret_cast<unsigned char *>(
        reinterpret_cast<std::uintptr_t>(exe) + kMenuHoverRva);

    if (!same_bytes(target, kMenuHoverSig, kMenuHoverLen)) {
        return false;
    }

    void *tramp = make_tramp(target, kMenuHoverLen);
    if (!tramp) {
        return false;
    }

    menu_hover_orig = reinterpret_cast<MenuHoverFn>(tramp);
    if (!put_jmp(target, kMenuHoverLen, reinterpret_cast<void *>(&menu_hover_hook))) {
        return false;
    }

    menu_hover_hooked = true;
    return true;
}

DWORD WINAPI hook_thread(void *) {
    for (;;) {
        HMODULE exe = GetModuleHandleW(nullptr);
        if (exe) {
            if (!xaudio_hooked) {
                hook_xaudio(exe);
            }

            if (!menu_hover_hooked) {
                hook_menu_hover(exe);
            }

            if (xaudio_hooked && menu_hover_hooked) {
                return 0;
            }
        }

        Sleep(500);
    }
}

}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);

        HANDLE thread = CreateThread(nullptr, 0, hook_thread, nullptr, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    }

    return TRUE;
}
