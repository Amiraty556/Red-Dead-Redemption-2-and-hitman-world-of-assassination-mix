// ArthurContracts - own rage::scrThread (port of rdr2scripthook's GtaThread
// path, MIT, Tuxick/rdr2scripthook; credited in CREDITS).
//
// Why: natives that build/draw text behave only while an ACTIVE script thread
// is set in the currentScriptThread global. The UpdateSingleScripts detour
// runs after the game's script pass, when that global is NULL - so we
// register OUR OWN thread and let the game tick it (TickScriptThread), with
// Run() setting the active thread around our per-frame code, exactly like the
// reference implementation.
//
// ABI NOTE: the game is MSVC-built (one vtable slot per virtual dtor).
// GCC/MinGW emits TWO (complete + deleting) which would shift every slot and
// crash the game - so no C++ virtuals are used here at all: the vtable is an
// explicit array in MSVC order, written into the object by hand.
#pragma once
#include <cstdint>
#include <cstring>

#include "log.hpp"

namespace rage {

using scrProgramId = uint32_t;

enum eThreadState : uint32_t {
    ThreadStateIdle,
    ThreadStateRunning,
    ThreadStateKilled,
    ThreadStateUnk3,
    ThreadStateUnk4,
};

struct scrThreadContext {
    uint32_t threadId;
    uint32_t scriptHash;
    eThreadState scriptState;
    uint32_t unused1;
    uint32_t unused2;
    uint32_t unused3;
    uint32_t timerA;
    uint32_t timerB;
    uint32_t timerC;
    uint32_t unk1;
    uint32_t unk2;
   private:
    char padding002C[0x34];
   public:
    uint32_t unk3;
   private:
    char padding0064[0x64C];
};
static_assert(sizeof(scrThreadContext) == 0x6B0, "scrThreadContext size");

// Layout of the game's rage::scrThread with vptr at 0 (verified against the
// reference's VALIDATE_SIZE(scrThread, 0x788) and its Reset() offset pokes).
struct scrThread {
    void* vptr_;                    // 0x000 - our manual vtable
    scrThreadContext context;       // 0x008
    uintptr_t stack;                // 0x6B8
    char padding00C0[0x10];         // 0x6C0
    char* exitMessage;              // 0x6D0
    uint32_t scriptHash;            // 0x6D8
    char pad_afterHash[4];          // 0x6DC
    void* scriptHandler;            // 0x6E0
    char padding06E8[0xA0];         // 0x6E8 .. 0x787
};
static_assert(sizeof(scrThread) == 0x788, "scrThread size");

}  // namespace rage

// The game's scriptHandlerMgr instance (rip-resolved by pattern), called by
// manual MSVC vtable index - slot list (MSVC: one dtor slot):
// 0 ~scriptHandlerMgr, 1 Init, 2 Update, 3 Shutdown, 4 NetworkInit,
// 5 NetworkUpdate, 6 NetworkShutdown, 7 GetScriptId, 8 CreateScriptHandler,
// 9 GetScriptHandler, 10 RegisterScript, 11 UnregisterScript.
namespace ac {
inline void RegisterScriptOnMgr(void* mgr, rage::scrThread* thread) {
    if (!mgr) return;
    using Fn = void (*)(void*, rage::scrThread&);
    Fn fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(mgr))[10]);
    fn(mgr, *thread);
}
}  // namespace ac
