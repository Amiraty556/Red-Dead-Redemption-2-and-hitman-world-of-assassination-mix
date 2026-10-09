// ArthurContracts - AOB pattern scanner over a PE module.
// Patterns come from sheets/hooks.json; every miss must be logged, never fatal.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ac {

struct PatternByte {
    uint8_t value;
    bool wildcard;
};

// "48 8B 15 ? ? ? ? 4C 8B C9 49 F7 D1" -> bytes
inline std::vector<PatternByte> ParsePattern(const std::string& pat) {
    std::vector<PatternByte> out;
    size_t i = 0;
    while (i < pat.size()) {
        while (i < pat.size() && pat[i] == ' ') i++;
        if (i >= pat.size()) break;
        if (pat[i] == '?') {
            out.push_back({0, true});
            i += (i + 1 < pat.size() && pat[i + 1] == '?') ? 2 : 1;
        } else if (i + 1 < pat.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            int hi = hex(pat[i]), lo = hex(pat[i + 1]);
            if (hi < 0 || lo < 0) return {};
            out.push_back({(uint8_t)((hi << 4) | lo), false});
            i += 2;
        } else {
            break;
        }
    }
    return out;
}

// Scan the main module image. Returns absolute address or 0.
inline uintptr_t FindPattern(const std::string& pat, intptr_t offset = 0) {
    auto patb = ParsePattern(pat);
    if (patb.empty()) return 0;
    HMODULE mod = GetModuleHandleA(nullptr);
    auto* dos = (IMAGE_DOS_HEADER*)mod;
    auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)mod + dos->e_lfanew);
    uint8_t* base = (uint8_t*)mod;
    size_t size = nt->OptionalHeader.SizeOfImage;
    if (patb.size() > size) return 0;
    for (size_t i = 0; i + patb.size() <= size; i++) {
        size_t j = 0;
        for (; j < patb.size(); j++) {
            if (patb[j].wildcard) continue;
            if (base[i + j] != patb[j].value) break;
        }
        if (j == patb.size()) {
            return (uintptr_t)(base + i) + offset;
        }
    }
    return 0;
}

// Read a RIP-relative target: at `instrAddr`, the instruction's disp32 at
// `dispRva` from instruction start points at data; returns absolute data addr.
template <typename T>
inline T ResolveRipRelative(uintptr_t instrAddr, int dispRva) {
    int32_t disp;
    memcpy(&disp, (void*)(instrAddr + dispRva), sizeof(disp));
    return (T)(instrAddr + dispRva + 4 + disp);
}

}  // namespace ac
