// ArthurContracts - minimal Glacier RPKG reader (own implementation).
//
// Format verified against this PC's HITMAN 3 install (runtime\chunk0.rpkg,
// 2026-10-09). Recon done with glacier-modding RPKG-Tool (GPL): constants and
// layout observed from real files + published spec; NO GPL code is copied.
//
//   magic '2KPR' = RPKGv2: u32 @0x0D hash_count, @0x11 hash_header_table_size
//                  (@0x15 hash_resource_table_size); hash table starts @0x19.
//   magic 'GKPR' = RPKGv1: u32 @0x04 hash_count, @0x08 header table size,
//                  @0x0C resource table size, @0x10 patch_count;
//                  hash table starts @0x14 + patch_count*8.
//   hash entry (20 bytes, packed): { u64 hash, u64 data_offset, u32 data_size }
//   data_size: low 30 bits = stored byte count (LZ4 block when != 0),
//              bit 0x80000000 = XORed with an 8-byte repeating key.
//   stored == 0 means "raw, size in the resource table" - unsupported here
//   (our three contract JSONs are always LZ4+XOR; verified on real files).
//
// Patch override files: verified 2026-10-09 that none of our three hashes is
// overridden in any chunk0patch*.rpkg, so chunk0.rpkg alone is authoritative.
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

#include "lz4dec.hpp"

namespace ac {

class Rpkg {
public:
    ~Rpkg() { Close(); }

    void Close() {
        if (file_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
        }
        entries_.clear();
    }

    bool Open(const std::string& path, std::string& err) {
        Close();
        file_ = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) {
            err = "open failed err=" + std::to_string(GetLastError());
            return false;
        }
        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(file_, &sz)) {
            err = "size failed";
            Close();
            return false;
        }
        fileSize_ = (uint64_t)sz.QuadPart;

        uint8_t head[0x20] = {0};
        if (!ReadAt(0, head, sizeof(head))) {
            err = "header read failed";
            Close();
            return false;
        }

        uint64_t hc = 0, hh = 0, tableBase = 0;
        if (head[0] == '2' && head[1] == 'K' && head[2] == 'P' && head[3] == 'R') {
            // RPKGv2
            memcpy(&hc, head + 0x0D, 4);
            memcpy(&hh, head + 0x11, 4);
            tableBase = 0x19;
        } else if (head[0] == 'G' && head[1] == 'K' && head[2] == 'P' && head[3] == 'R') {
            // RPKGv1
            uint32_t pc = 0;
            memcpy(&hc, head + 0x04, 4);
            memcpy(&hh, head + 0x08, 4);
            memcpy(&pc, head + 0x10, 4);
            tableBase = 0x14ull + (uint64_t)pc * 8;
        } else {
            err = "unknown magic";
            Close();
            return false;
        }

        if (hc == 0 || hc > 8000000ull || hh != hc * 20) {
            err = "implausible header hc=" + std::to_string(hc) +
                  " hh=" + std::to_string(hh);
            Close();
            return false;
        }
        if (tableBase + hh > fileSize_) {
            err = "table past EOF";
            Close();
            return false;
        }

        entries_.resize((size_t)hc);
        // Stream the hash table in bounded chunks.
        const uint64_t kChunk = 4ull * 1024 * 1024;
        uint64_t off = tableBase;
        uint8_t* dst = (uint8_t*)entries_.data();
        uint64_t remaining = hh;
        while (remaining) {
            uint64_t n = remaining < kChunk ? remaining : kChunk;
            if (!ReadAt(off, dst, (size_t)n)) {
                err = "table read failed";
                Close();
                return false;
            }
            off += n;
            dst += n;
            remaining -= n;
        }

        // Sanity: first entry must look like an IOI hash (top byte 0).
        if ((entries_[0].hash >> 56) != 0) {
            err = "first hash implausible (layout?)";
            Close();
            return false;
        }
        return true;
    }

    size_t Count() const { return entries_.size(); }

    // Extract a resource: read stored bytes -> optional XOR -> LZ4 into out.
    // out is resized to the decoded length. cap bounds the decoded size.
    bool Extract(uint64_t hash, std::vector<char>& out, uint32_t cap, std::string& err) {
        if (file_ == INVALID_HANDLE_VALUE) {
            err = "not open";
            return false;
        }
        const Entry* e = nullptr;
        for (const auto& c : entries_)
            if (c.hash == hash) { e = &c; break; }
        if (!e) {
            err = "hash not in table";
            return false;
        }

        const uint32_t dsz = e->size;
        const uint32_t stored = dsz & 0x3FFFFFFF;
        const bool xored = (dsz & 0x80000000) != 0;
        if (stored == 0) {
            err = "raw resource (needs resource table) unsupported";
            return false;
        }
        if (e->off + stored > fileSize_) {
            err = "data past EOF";
            return false;
        }

        std::vector<char> buf((size_t)stored);
        if (!ReadAt(e->off, buf.data(), stored)) {
            err = "data read failed err=" + std::to_string(GetLastError());
            return false;
        }
        if (xored) {
            static const uint8_t kKey[8] = {0xDC, 0x45, 0xA6, 0x9C,
                                            0xD3, 0x72, 0x4C, 0xAB};
            for (uint32_t i = 0; i < stored; i++)
                buf[i] = (char)((uint8_t)buf[i] ^ kKey[i % 8]);
        }

        out.resize(cap);
        int n = Lz4Decompress(buf.data(), (int)stored, out.data(), (int)cap);
        if (n < 0) {
            err = "lz4 decode failed";
            out.clear();
            return false;
        }
        out.resize((size_t)n);
        return true;
    }

private:
    struct Entry {
        uint64_t hash;
        uint64_t off;
        uint32_t size;
    };

    bool ReadAt(uint64_t off, void* buf, size_t n) {
        LARGE_INTEGER li;
        li.QuadPart = (LONGLONG)off;
        if (!SetFilePointerEx(file_, li, nullptr, FILE_BEGIN)) return false;
        size_t done = 0;
        while (done < n) {
            DWORD got = 0;
            if (!ReadFile(file_, (uint8_t*)buf + done, (DWORD)(n - done), &got, nullptr))
                return false;
            if (got == 0) return false;
            done += got;
        }
        return true;
    }

    HANDLE file_ = INVALID_HANDLE_VALUE;
    uint64_t fileSize_ = 0;
    std::vector<Entry> entries_;
};

}  // namespace ac
