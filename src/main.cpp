// ArthurContracts - entry point (vertical slice).
//
// Slice proves: UAL loads the ASI -> patterns resolve -> tick fires in script
// context -> natives answer -> one HITMAN-flavoured line renders in game.
// Every failure path must leave the game running and log why.
#include <windows.h>
#include <MinHook.h>
#include <cstdlib>

#include "hitman.hpp"
#include "log.hpp"
#include "natives.hpp"
#include "pattern.hpp"
#include "thread.hpp"

// ---- sheets/natives.json subset used by the slice --------------------------
namespace N {
constexpr uint64_t GET_GAME_TIMER = 0x4F67E8ECA7D3F667;
constexpr uint64_t PLAYER_PED_ID = 0x096275889B8E0EE0;
constexpr uint64_t VAR_STRING = 0xFA925AC00EB830B9;
constexpr uint64_t DISPLAY_TEXT = 0xD79334A4BB99BAD1;
constexpr uint64_t SET_TEXT_COLOR = 0x50A41AD966910F03;
constexpr uint64_t SET_TEXT_SCALE = 0x4170B650590B3B00;
// Alternate "_BG" text stack: drawn on the background text queue, used when
// the main HUD text queue is suppressed (cutscenes, menus).
constexpr uint64_t BG_DISPLAY_TEXT = 0x16794E044C9EFB58;
constexpr uint64_t BG_SET_TEXT_COLOR = 0x16FA5CE47F184F1E;
constexpr uint64_t BG_SET_TEXT_SCALE = 0xA1253A3C870B6843;
}  // namespace N

namespace {
using UpdateSingleScripts_t = bool (*)(void*);
UpdateSingleScripts_t o_update_single_scripts = nullptr;

uintptr_t g_getNativeAddress = 0;
uintptr_t g_tick = 0;
uintptr_t g_tick_fallback = 0;
uintptr_t g_threadGlobal = 0;  // address of the rage::scrThread* active-thread global

// Own-script-thread path (see thread.hpp): resolved game functions.
void (*g_initScriptThread)(rage::scrThread*) = nullptr;
rage::eThreadState (*g_tickScriptThread)(rage::scrThread*, uint32_t) = nullptr;
void (*g_killScriptThread)(rage::scrThread*) = nullptr;
void* g_scriptHandlerMgr = nullptr;
bool* g_isInSession = nullptr;
rage::scrThread* g_thread = nullptr;
bool g_loggedThreadPath = false;
bool g_loggedOnline = false;

uint64_t g_ticks = 0;
int g_drawFrames = 0;
bool g_worldReady = false;

// Real HITMAN contract data, read from the player's own install (hitman.hpp).
// Load happens once, at world ready.
bool g_hitmanLoaded = false;
ac::HitmanContract g_contracts[3];  // [0]=hc_bulldog [1]=hc_llama [2]=hc_rat

// Our own MIT text (sheets/contracts.json), index = difficulty order
// rat(1) -> bulldog(2) -> llama(3). Used verbatim when install data is missing.
static const char* kSheetCodename[3] = {"Rat", "Bulldog", "Llama"};
static const char* kSheetStaging[3] = {
    "Saint Denis back streets (night stand-in for Chongqing)",
    "Braithwaite Manor (stand-in for Thornbridge Manor)",
    "Road south of Rhodes (stand-in for Mendoza vineyard)",
};
static const char* kSheetBrief[3] = {
    "Two targets, one rain-slick street. No witnesses, no noise.",
    "An estate, a lady of the house. The Case File is secondary.",
    "A carriage, two targets. Do not eliminate Diana Burnwood.",
};

const char* Lit(const char* text) {
    return (const char*)ac::Invoke<uint64_t>(
        N::VAR_STRING, (uint32_t)10, (uintptr_t)"LITERAL_STRING", (uintptr_t)text);
}

void DrawLine(const char* text, float x, float y, int r, int g, int b) {
    const char* s = Lit(text);
    if (!s) return;
    ac::Invoke<void>(N::SET_TEXT_SCALE, 0.20f, 0.20f);
    ac::Invoke<void>(N::SET_TEXT_COLOR, r, g, b, 255);
    ac::Invoke<void>(N::DISPLAY_TEXT, (uintptr_t)s, x, y);
}

void DrawLineBg(const char* text, float x, float y, int r, int g, int b) {
    const char* s = Lit(text);
    if (!s) return;
    ac::Invoke<void>(N::BG_SET_TEXT_SCALE, 0.20f, 0.20f);
    ac::Invoke<void>(N::BG_SET_TEXT_COLOR, r, g, b, 255);
    ac::Invoke<void>(N::BG_DISPLAY_TEXT, (uintptr_t)s, x, y);
}

void* ScriptThread() {
    return g_threadGlobal ? *(void**)g_threadGlobal : nullptr;
}

void OnTick() {
    g_ticks++;
    if (!ac::NativeResolver()) return;

    if (!g_worldReady) {
        uint64_t ped = ac::Invoke<uint64_t>(N::PLAYER_PED_ID);
        if (ped) {
            g_worldReady = true;
            AC_LOG("WORLD READY playerPed=0x%llX tick=%llu scriptThread=%p",
                   (unsigned long long)ped, (unsigned long long)g_ticks, ScriptThread());
            AC_LOG("first draw probe VAR_STRING=%p",
                   (const void*)Lit("HITMAN CONTRACT  //  Arthur Morgan"));
            if (!g_hitmanLoaded) {
                g_hitmanLoaded = true;
                ac::LoadContracts(g_contracts);  // logs every step (hitman: ...)
            }
        }
        if (g_ticks % 600 == 0)
            AC_LOG("waiting for world tick=%llu timer=%u thread=%p", (unsigned long long)g_ticks,
                   ac::Invoke<uint32_t>(N::GET_GAME_TIMER), ScriptThread());
        return;
    }

    if (g_ticks % 600 == 0)
        AC_LOG("tick=%llu gameTimer=%u thread=%p", (unsigned long long)g_ticks,
               ac::Invoke<uint32_t>(N::GET_GAME_TIMER), ScriptThread());

    // v1 dossier: rotates the three contracts (difficulty order), showing
    // REAL install data (codename + primary objective) over our own briefs.
    // Both text stacks are driven so a queue that is suppressed still has a twin.
    g_drawFrames++;
    static const int kOrder[3] = {2, 0, 1};  // g_contracts idx for rat, bulldog, llama
    const int slot = (int)((g_ticks / 900) % 3);
    const ac::HitmanContract& hc = g_contracts[kOrder[slot]];
    static char l1[160], l2[192], l3[192], l4[192];

    const char* name = (hc.ok && !hc.codename.empty()) ? hc.codename.c_str()
                                                       : kSheetCodename[slot];
    snprintf(l1, sizeof(l1), "CONTRACT %d/3  //  %s", slot + 1, name);
    if (hc.ok && !hc.objectives.empty())
        snprintf(l2, sizeof(l2), "PRIMARY: %s", hc.objectives[0].c_str());
    else if (!hc.err.empty())
        snprintf(l2, sizeof(l2), "contract file unavailable (%s)", hc.err.c_str());
    else
        snprintf(l2, sizeof(l2), "contract file unavailable");
    snprintf(l3, sizeof(l3), "STAGING: %s", kSheetStaging[slot]);
    snprintf(l4, sizeof(l4), "%s", kSheetBrief[slot]);

    DrawLine(l1, 0.03f, 0.05f, 255, 180, 40);
    DrawLine(l2, 0.03f, 0.08f, 230, 230, 230);
    DrawLine(l3, 0.03f, 0.11f, 230, 230, 230);
    DrawLine(l4, 0.03f, 0.14f, 200, 200, 200);
    DrawLineBg(l1, 0.03f, 0.19f, 255, 180, 40);
    DrawLineBg(l4, 0.03f, 0.22f, 200, 200, 200);
    if (g_drawFrames == 300)
        AC_LOG("drawing ok frames=300 thread=%p slot=%d", ScriptThread(), slot);
}

// ---- own script thread: our OnTick runs inside its Run() with the active
// ---- thread global pointing at it (port of the reference's GtaThread path).
void SetActiveThread(rage::scrThread* t) {
    if (g_threadGlobal) *reinterpret_cast<rage::scrThread**>(g_threadGlobal) = t;
}
rage::scrThread* GetActiveThread() {
    return g_threadGlobal ? *reinterpret_cast<rage::scrThread**>(g_threadGlobal) : nullptr;
}

void V_Dtor(rage::scrThread*) {}

rage::eThreadState V_Reset(rage::scrThread* self, rage::scrProgramId hash,
                            const void*, int) {
    memset(&self->context, 0, sizeof(self->context));
    self->context.scriptState = rage::ThreadStateIdle;
    self->context.scriptHash = hash;
    self->context.unk1 = (uint32_t)-1;
    self->context.unk2 = (uint32_t)-1;
    self->context.unk3 = 1;

    auto* base = reinterpret_cast<uint8_t*>(self);
    *reinterpret_cast<uint64_t*>(base + 0x720) = 0;
    *reinterpret_cast<uint32_t*>(base + 0x728) = 0;
    *reinterpret_cast<uint64_t*>(base + 0x730) = 0;
    *reinterpret_cast<uint32_t*>(base + 0x738) = 0;
    *reinterpret_cast<uint64_t*>(base + 0x740) = 0;
    *reinterpret_cast<uint32_t*>(base + 0x748) = 0;
    *reinterpret_cast<uint64_t*>(base + 0x750) = 0;
    *reinterpret_cast<uint32_t*>(base + 0x758) = 0;
    *reinterpret_cast<uint64_t*>(base + 0x760) = 0;
    *reinterpret_cast<uint32_t*>(base + 0x768) = 0;
    *reinterpret_cast<uint8_t*>(base + 0x770) = 0;
    *reinterpret_cast<uint64_t*>(base + 0x6E0) = 0;
    *reinterpret_cast<bool*>(base + 0x71A) = false;

    if (g_initScriptThread) g_initScriptThread(self);
    self->exitMessage = const_cast<char*>("Not aborted yet?");
    self->context.threadId = hash;

    ac::RegisterScriptOnMgr(g_scriptHandlerMgr, self);
    AC_LOG("script thread RESET+registered id=0x%X mgr=%p", hash, g_scriptHandlerMgr);
    return self->context.scriptState;
}

rage::eThreadState V_Run(rage::scrThread* self, int) {
    rage::scrThread* prev = GetActiveThread();
    SetActiveThread(self);
    if (self->context.scriptState != rage::ThreadStateKilled) OnTick();
    SetActiveThread(prev);
    return self->context.scriptState;
}

rage::eThreadState V_Update(rage::scrThread* self, int operationCount) {
    if (!g_tickScriptThread) return rage::ThreadStateKilled;
    return g_tickScriptThread(self, (uint32_t)operationCount);
}

void V_Kill(rage::scrThread* self) {
    if (g_killScriptThread) g_killScriptThread(self);
}

// (thread vtable is built lazily in DriveThread)

// Called after the game's own script pass each frame (from the detour).
void DriveThread() {
    if (g_isInSession && *g_isInSession) {
        if (!g_loggedOnline) {
            AC_LOG("ONLINE SESSION detected - mod idles (story mode only)");
            g_loggedOnline = true;
        }
        return;
    }

    if (!g_thread) {
        const bool pathOk = g_initScriptThread && g_tickScriptThread &&
                            g_scriptHandlerMgr && g_threadGlobal;
        if (!pathOk) {
            // Best effort: call natives without thread context (some draw).
            OnTick();
            return;
        }
        if (!ac::Invoke<uint64_t>(N::PLAYER_PED_ID)) return;  // world not up yet
        static void* vt[5];
        if (!vt[0]) {
            vt[0] = (void*)&V_Dtor;
            vt[1] = (void*)&V_Reset;
            vt[2] = (void*)&V_Run;
            vt[3] = (void*)&V_Update;
            vt[4] = (void*)&V_Kill;
        }
        g_thread = (rage::scrThread*)calloc(1, 0x788);
        if (!g_thread) return;
        g_thread->vptr_ = vt;
        uint32_t id = 0x10001;
        g_thread->scriptHash = id;
        V_Reset(g_thread, id, nullptr, 0);
        g_loggedThreadPath = true;
        AC_LOG("thread path ACTIVE (own script thread)");
    }
    if (g_thread && g_tickScriptThread)
        g_tickScriptThread(g_thread, 13000000);
}

bool __fastcall HookedUpdateSingleScripts(void* collection) {
    bool r = o_update_single_scripts ? o_update_single_scripts(collection) : false;
    DriveThread();
    return r;
}

using IsDlcPresent_t = int(__fastcall*)(__int64, uint32_t);
IsDlcPresent_t o_is_dlc_present = nullptr;
int __fastcall HookedIsDlcPresent(__int64 a1, uint32_t dlc) {
    DriveThread();
    return o_is_dlc_present ? o_is_dlc_present(a1, dlc) : 0;
}

}  // namespace

DWORD WINAPI InitThread(LPVOID) {
    AC_LOG("=== ArthurContracts slice starting ===");

    AC_LOG("step: scanning native resolver pattern");
    // hooks.json hk_get_native_address: the pattern STARTS the function
    g_getNativeAddress = ac::FindPattern("48 8B 15 ? ? ? ? 4C 8B C9 49 F7 D1");
    AC_LOG("step: scan done -> %p", (void*)g_getNativeAddress);
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

    // hooks.json hk_current_script_thread: resolve the ADDRESS OF the global
    // (rip-relative disp32 at +3), then read its value for diagnostics.
    uintptr_t curThread = ac::FindPattern("48 39 1D ? ? ? ? 75 ? 48 8D 05 ? ? ? ?");
    if (curThread) g_threadGlobal = ac::ResolveRipRelative<uintptr_t>(curThread, 3);
    AC_LOG("current_script_thread sig: %s global=%p value=%p",
           curThread ? "RESOLVED" : "not found", (void*)g_threadGlobal, ScriptThread());

    // Own-thread path: game functions + mgr instance + online flag.
    // Patterns verbatim from rdr2scripthook (MIT).
    uintptr_t p = ac::FindPattern("48 89 5C 24 ? 57 48 83 EC ? 83 89 ? ? ? ? ? 33 FF 83 A1");
    if (p) g_initScriptThread = (void (*)(rage::scrThread*))p;
    AC_LOG("init_script_thread: %s (%p)", p ? "RESOLVED" : "not found", (void*)p);

    p = ac::FindPattern("48 89 5C 24 ? 57 48 83 EC ? 80 B9 ? ? ? ? ? 8B FA 48 8B D9 74 ? 8B 41");
    if (p) g_tickScriptThread = (rage::eThreadState (*)(rage::scrThread*, uint32_t))p;
    AC_LOG("tick_script_thread: %s (%p)", p ? "RESOLVED" : "not found", (void*)p);

    p = ac::FindPattern("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F9 8B 49");
    if (p) g_killScriptThread = (void (*)(rage::scrThread*))p;
    AC_LOG("kill_script_thread: %s (%p)", p ? "RESOLVED" : "not found", (void*)p);

    p = ac::FindPattern("48 8D 0D ? ? ? ? E8 ? ? ? ? 84 C0 75 ? 8B 8B ? ? ? ?");
    if (p) g_scriptHandlerMgr = (void*)ac::ResolveRipRelative<uintptr_t>(p, 3);
    AC_LOG("script_handler_mgr: %s (%p)", p ? "RESOLVED" : "not found", g_scriptHandlerMgr);

    p = ac::FindPattern("80 3D ? ? ? ? ? 74 ? 48 8B 0D ? ? ? ? E8 ? ? ? ? 0F B6 40 ?");
    if (p) g_isInSession = (bool*)ac::ResolveRipRelative<uintptr_t>(p, 2);
    AC_LOG("is_in_session: %s (%p value=%d)", p ? "RESOLVED" : "not found", g_isInSession,
           (g_isInSession && p) ? (int)*g_isInSession : -1);

    AC_LOG("self-test GET_GAME_TIMER = %u", ac::Invoke<uint32_t>(N::GET_GAME_TIMER));
    AC_LOG("=== slice init done ===");
    return 0;
}

// Log only genuinely fatal exceptions, with a recursion guard: first-chance
// exception traffic (dbg/stack-growth codes) must never flood the log or
// re-enter the logger.
LONG WINAPI VehLog(EXCEPTION_POINTERS* e) {
    if (!e || !e->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = e->ExceptionRecord->ExceptionCode;
    bool fatal = code == 0xC0000005 /* AV */ || code == 0xC00000FD /* stack overflow */ ||
                 code == 0xC000001D /* illegal instr */ || code == 0xC0000094 /* div0 */ ||
                 code == 0xC0000025 /* non-continuable */ || code == 0xC0000409 /* fail fast */ ||
                 code == 0xE06D7363 /* C++ throw */;
    if (!fatal) return EXCEPTION_CONTINUE_SEARCH;
    static volatile LONG busy = 0;
    if (InterlockedCompareExchange(&busy, 1, 0) != 0) return EXCEPTION_CONTINUE_SEARCH;
    AC_LOG("CRASH exception=0x%lX at %p", (unsigned long)code,
           e->ExceptionRecord->ExceptionAddress);
    busy = 0;
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Boot marker FIRST: proves the loader ran us at all, before any
        // other init that could fail. File beside the ASI + the usual log.
        char base[MAX_PATH];
        DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
        if (n && n < MAX_PATH) {
            std::string dir = std::string(base, n) + "\\ArthurContracts";
            CreateDirectoryA(dir.c_str(), nullptr);
            FILE* f = nullptr;
            fopen_s(&f, (dir + "\\log.txt").c_str(), "a");
            if (f) { fprintf(f, "[boot] DllMain PROCESS_ATTACH static-loaded\n"); fclose(f); }
        }
        DisableThreadLibraryCalls(hModule);
        AddVectoredExceptionHandler(1, VehLog);
        HANDLE th = CreateThread(nullptr, 0, InitThread, hModule, 0, nullptr);
        if (th) CloseHandle(th);
        else {
            FILE* f = nullptr;
            fopen_s(&f, (std::string(base, n) + "\\ArthurContracts\\log.txt").c_str(), "a");
            if (f) { fprintf(f, "[boot] CreateThread FAILED err=%lu\n", GetLastError()); fclose(f); }
        }
    }
    return TRUE;
}
