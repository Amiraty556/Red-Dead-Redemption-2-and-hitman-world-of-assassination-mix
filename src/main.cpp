// ArthurContracts - entry point (vertical slice).
//
// Slice proves: UAL loads the ASI -> patterns resolve -> tick fires in script
// context -> natives answer -> one HITMAN-flavoured line renders in game.
// Every failure path must leave the game running and log why.
#include <windows.h>
#include <MinHook.h>

#include "log.hpp"
#include "natives.hpp"
#include "pattern.hpp"

// ---- sheets/natives.json subset used by the slice --------------------------
namespace N {
constexpr uint64_t GET_GAME_TIMER = 0x4F67E8ECA7D3F667;
constexpr uint64_t PLAYER_PED_ID = 0x096275889B8E0EE0;
constexpr uint64_t VAR_STRING = 0xFA925AC00EB830B9;
constexpr uint64_t DISPLAY_TEXT = 0xD79334A4BB99BAD1;
constexpr uint64_t SET_TEXT_COLOR = 0x50A41AD966910F03;
constexpr uint64_t SET_TEXT_SCALE = 0x4170B650590B3B00;
}  // namespace N

namespace {
using UpdateSingleScripts_t = bool (*)(void*);
UpdateSingleScripts_t o_update_single_scripts = nullptr;

uintptr_t g_getNativeAddress = 0;
uintptr_t g_tick = 0;
uintptr_t g_tick_fallback = 0;

uint64_t g_ticks = 0;
int g_drawFrames = 0;
bool g_worldReady = false;

void DrawLine(const char* text, float x, float y, int r, int g, int b) {
    const char* s = (const char*)ac::Invoke<uint64_t>(
        N::VAR_STRING, (uint32_t)10, (uintptr_t)"LITERAL_STRING", (uintptr_t)text);
    if (!s) return;
    ac::Invoke<void>(N::SET_TEXT_SCALE, 0.20f, 0.20f);
    ac::Invoke<void>(N::SET_TEXT_COLOR, r, g, b, 255);
    ac::Invoke<void>(N::DISPLAY_TEXT, (uintptr_t)s, x, y);
}

void OnTick() {
    g_ticks++;
    if (!ac::NativeResolver()) return;

    if (!g_worldReady) {
        uint64_t ped = ac::Invoke<uint64_t>(N::PLAYER_PED_ID);
        if (ped) {
            g_worldReady = true;
            AC_LOG("WORLD READY playerPed=0x%llX tick=%llu", (unsigned long long)ped,
                   (unsigned long long)g_ticks);
        }
        if (g_ticks % 600 == 0)
            AC_LOG("waiting for world tick=%llu timer=%u", (unsigned long long)g_ticks,
                   ac::Invoke<uint32_t>(N::GET_GAME_TIMER));
        return;
    }

    if (g_ticks % 600 == 0)
        AC_LOG("tick=%llu gameTimer=%u", (unsigned long long)g_ticks,
               ac::Invoke<uint32_t>(N::GET_GAME_TIMER));

    // Slice proof: render a line for ~10s of gameplay.
    if (g_drawFrames < 600) {
        g_drawFrames++;
        DrawLine("ARTHUR CONTRACTS - dossier loading (slice test)", 0.03f, 0.05f, 255, 180, 40);
    }
}

bool __fastcall HookedUpdateSingleScripts(void* collection) {
    bool r = o_update_single_scripts ? o_update_single_scripts(collection) : false;
    OnTick();
    return r;
}

using IsDlcPresent_t = int(__fastcall*)(__int64, uint32_t);
IsDlcPresent_t o_is_dlc_present = nullptr;
int __fastcall HookedIsDlcPresent(__int64 a1, uint32_t dlc) {
    OnTick();
    return o_is_dlc_present ? o_is_dlc_present(a1, dlc) : 0;
}

}  // namespace

DWORD WINAPI InitThread(LPVOID) {
    AC_LOG("=== ArthurContracts slice starting ===");

    // hooks.json hk_get_native_address: the pattern STARTS the function
    g_getNativeAddress = ac::FindPattern("48 8B 15 ? ? ? ? 4C 8B C9 49 F7 D1");
    if (!g_getNativeAddress) {
        AC_LOG("NATIVE_RESOLVER_MISSING - mod disabled, game unaffected");
        return 0;
    }
    ac::NativeResolver() = (ac::GetNativeAddressFn)g_getNativeAddress;
    AC_LOG("RESOLVED get_native_address = %p", (void*)g_getNativeAddress);

    if (MH_Initialize() != MH_OK) {
        AC_LOG("MH_Initialize FAILED - mod disabled");
        return 0;
    }

    // hooks.json hk_tick (primary)
    g_tick = ac::FindPattern(
        "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 45 33 F6 BD ? ? ? ?");
    if (g_tick) {
        if (MH_CreateHook((void*)g_tick, (void*)HookedUpdateSingleScripts,
                          (void**)&o_update_single_scripts) == MH_OK &&
            MH_EnableHook((void*)g_tick) == MH_OK) {
            AC_LOG("RESOLVED tick update_single_scripts = %p", (void*)g_tick);
        } else {
            AC_LOG("tick hook install FAILED (MH error)");
            g_tick = 0;
        }
    }
    // hooks.json hk_tick_fallback
    if (!g_tick) {
        g_tick_fallback = ac::FindPattern("81 FA 00 00 00 00 75 0B 8A", -0x1E);
        if (g_tick_fallback &&
            MH_CreateHook((void*)g_tick_fallback, (void*)HookedIsDlcPresent,
                          (void**)&o_is_dlc_present) == MH_OK &&
            MH_EnableHook((void*)g_tick_fallback) == MH_OK) {
            AC_LOG("RESOLVED tick fallback is_dlc_present = %p", (void*)g_tick_fallback);
        } else {
            AC_LOG("TICK_NOT_FOUND - mod idles, game unaffected");
        }
    }

    // hooks.json hk_current_script_thread (resolved, informational for slice)
    uintptr_t curThread = ac::FindPattern("48 39 1D ? ? ? ? 75 ? 48 8D 05 ? ? ? ?");
    AC_LOG("current_script_thread sig: %s (%p)", curThread ? "RESOLVED" : "not found",
           (void*)curThread);

    AC_LOG("self-test GET_GAME_TIMER = %u", ac::Invoke<uint32_t>(N::GET_GAME_TIMER));
    AC_LOG("=== slice init done ===");
    return 0;
}

// Log where we die, so a crash names itself (native.md: minidump on filter).
LONG WINAPI VehLog(EXCEPTION_POINTERS* e) {
    if (e && e->ExceptionRecord) {
        AC_LOG("CRASH exception=0x%lX at %p", (unsigned long)e->ExceptionRecord->ExceptionCode,
               e->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        AddVectoredExceptionHandler(1, VehLog);
        HANDLE th = CreateThread(nullptr, 0, InitThread, hModule, 0, nullptr);
        if (th) CloseHandle(th);
    }
    return TRUE;
}
