// ArthurContracts - minimal LZ4 block-format decoder (own implementation of
// the public LZ4 block format: token, literal run, 16-bit offset, match run).
// Used for Glacier RPKG resource data. Bounded: never writes past dstCap,
// returns -1 on malformed input or overflow. Not copied from lz4.git.
#pragma once
#include <cstdint>

namespace ac {

// Returns bytes written to dst, or -1 on error.
inline int Lz4Decompress(const char* src, int srcSize, char* dst, int dstCap) {
    const unsigned char* s = (const unsigned char*)src;
    const unsigned char* sEnd = s + (srcSize > 0 ? srcSize : 0);
    char* d = dst;
    char* const dEnd = dst + dstCap;
    char* const dStart = dst;

    while (s < sEnd) {
        unsigned token = *s++;

        // ---- literal run ----
        int litLen = token >> 4;
        if (litLen == 15) {
            unsigned b;
            do {
                if (s >= sEnd) return -1;
                b = *s++;
                litLen += (int)b;
            } while (b == 255);
        }
        if (litLen > 0) {
            if (s + litLen > sEnd || d + litLen > dEnd) return -1;
            for (int i = 0; i < litLen; i++) *d++ = (char)*s++;
        }
        if (s >= sEnd) break;  // last sequence: literals only (LZ4 end condition)

        // ---- match run ----
        if (s + 2 > sEnd) return -1;
        int off = (int)s[0] | ((int)s[1] << 8);
        s += 2;
        if (off == 0 || (d - dStart) < off) return -1;

        int mLen = (int)(token & 15) + 4;
        if ((token & 15) == 15) {
            unsigned b;
            do {
                if (s >= sEnd) return -1;
                b = *s++;
                mLen += (int)b;
            } while (b == 255);
        }
        if (d + mLen > dEnd) return -1;

        const char* from = d - off;
        for (int i = 0; i < mLen; i++) *d++ = from[i];  // byte copy: overlap-safe
    }
    return (int)(d - dStart);
}

}  // namespace ac
