// ArthurContracts - RAGE native invocation WITHOUT ScriptHookRDR2.
//
// Technique and Context layout follow the public references:
//   Device9999/RDR2-Invoker-Entrypoint-Dumper (public) and Tuxick's script
//   hook, as re-published in emcifuntik/rdr2scripthook (MIT).
//   CopyResults() is a mechanical port of the published implementation
//   (i64 literals replaced); it is the proven result copy-back for RDR2.
// Handler resolution: the game's own GetNativeAddress(hash), pattern scanned
// (sheets/hooks.json hk_get_native_address). Build-independent.
// Hashes/prototypes come from sheets/natives.json (alloc8or's DB).
#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ac {

struct NativeContext {
    // RAGE layout - do not reorder (offsets: retVal@0 argCount@8 stackPtr@16
    // dataCount@24 spaceForResults@32 stack@224).
    uint64_t* retVal;
    uint64_t argCount;
    uint64_t* stackPtr;
    uint64_t dataCount;
    uint64_t spaceForResults[24];
    uint64_t stack[24];

    NativeContext() {
        memset(stack, 0, sizeof(stack));
        Reset();
    }

    void Reset() {
        argCount = 0;
        dataCount = 0;
        retVal = stack;
        stackPtr = stack;
    }

    template <class T>
    void Push(T v) {
        static_assert(sizeof(T) <= 8, "arg too big");
        memcpy(&stack[argCount++], &v, sizeof(T));
    }

    // Verbatim port of the published CopyResults (proven on RDR2).
    void CopyResults() {
        uint64_t a1 = (uint64_t)this;
        uint64_t result;
        for (; *(uint32_t*)(a1 + 24);
             *(uint64_t*)(*(uint64_t*)(a1 + 8 * *(int32_t*)(a1 + 24) + 32) + 16) = result) {
            --*(uint32_t*)(a1 + 24);
            **(uint32_t**)(a1 + 8 * *(int32_t*)(a1 + 24) + 32) =
                *(uint32_t*)(a1 + 16 * (*(int32_t*)(a1 + 24) + 4));
            *(uint32_t*)(*(uint64_t*)(a1 + 8 * *(int32_t*)(a1 + 24) + 32) + 8) =
                *(uint32_t*)(a1 + 16 * *(int32_t*)(a1 + 24) + 68);
            result = *(uint32_t*)(a1 + 16 * *(int32_t*)(a1 + 24) + 72);
        }
        --*(uint32_t*)(a1 + 24);
    }

    template <class T>
    T Result() const {
        static_assert(sizeof(T) <= 8, "result too big");
        T v{};
        memcpy(&v, retVal, sizeof(T));
        return v;
    }
    // RDR2 vectors return as three 8-byte slots: floats at +0, +8, +16.
    struct Vec3 {
        float x, y, z;
    };
    Vec3 ResultVec3() const {
        Vec3 v;
        memcpy(&v.x, (uint8_t*)retVal + 0, 4);
        memcpy(&v.y, (uint8_t*)retVal + 8, 4);
        memcpy(&v.z, (uint8_t*)retVal + 16, 4);
        return v;
    }
};

using NativeHandler = void (*)(NativeContext*);
using GetNativeAddressFn = uintptr_t (*)(uint64_t hash);

inline GetNativeAddressFn& NativeResolver() {
    static GetNativeAddressFn fn = nullptr;
    return fn;
}

template <class R = uint64_t, class... Args>
R Invoke(uint64_t hash, Args... args) {
    NativeContext ctx;
    (ctx.Push(args), ...);
    if (auto fn = NativeResolver()) {
        if (auto handler = (NativeHandler)fn(hash)) handler(&ctx);
    }
    ctx.CopyResults();
    if constexpr (std::is_void_v<R>) return;
    else if constexpr (std::is_same_v<R, NativeContext::Vec3>) return ctx.ResultVec3();
    else return ctx.Result<R>();
}

// Joaat used for model/weapon/outfit name hashes (script convention).
inline uint32_t Joaat(const char* s) {
    uint32_t h = 0;
    for (const char* p = s; *p; ++p) {
        h += (uint8_t)*p;
        h += (h << 10);
        h ^= (h >> 6);
    }
    h += (h << 3);
    h ^= (h >> 11);
    h += (h << 15);
    return h;
}

}  // namespace ac
