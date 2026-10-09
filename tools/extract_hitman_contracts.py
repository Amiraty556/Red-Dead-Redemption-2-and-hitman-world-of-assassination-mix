#!/usr/bin/env python3
"""ArthurContracts - reference extractor for HITMAN contract resources.

Recon/verification tool (not shipped): parses the player's own
HITMAN 3\\runtime\\chunk0.rpkg (RPKG v1 'GKPR' / v2 '2KPR'), locates the three
contract JSONs by IOI hash, applies the resource transform (XOR key + LZ4
block), and writes ~/hm-extract/hc_{bulldog,llama,rat}.json.

Format constants observed from the real install + published spec; this script
is written from those observations (the GPL RPKG-Tool was used for recon only,
no code copied). The ASI contains its own C++ implementation (src/rpkg.hpp);
tools/rpkg_selftest.cpp compares the two byte-for-byte.
"""
import json
import os
import struct
import sys

try:
    import lz4.block
except ImportError:
    sys.exit("pip install lz4")

RUNTIME = r"D:\SteamLibrary\steamapps\common\HITMAN 3\runtime"
XOR_KEY = bytes([0xDC, 0x45, 0xA6, 0x9C, 0xD3, 0x72, 0x4C, 0xAB])
OUT_DIR = os.path.expanduser("~/hm-extract")

# id -> ioi hash (sheets/hitman_content.json, verified presentInInstall)
TARGETS = {
    "hc_bulldog": 0x00F1C98E21AC5D76,
    "hc_llama": 0x00C27E02CD7CD328,
    "hc_rat": 0x0092633EE2D15990,
}


def open_rpkg(path):
    f = open(path, "rb")
    magic = f.read(4)
    if magic == b"2KPR":  # RPKGv2
        f.seek(0x0D)
        hc, hh, hr = struct.unpack("<III", f.read(12))
        tbase = 0x19
    elif magic == b"GKPR":  # RPKGv1
        f.seek(0x04)
        hc, hh, hr, pc = struct.unpack("<IIII", f.read(16))
        tbase = 0x14 + pc * 8
    else:
        f.close()
        raise ValueError(f"bad magic {magic!r} in {path}")
    if hc == 0 or hh != hc * 20:
        f.close()
        raise ValueError(f"implausible header hc={hc} hh={hh} in {path}")
    return f, hc, tbase


def find_hash(f, hc, tbase, want):
    f.seek(tbase)
    off = tbase
    remaining = hc
    while remaining:
        n = min(remaining, 200_000)
        buf = f.read(n * 20)
        for i in range(len(buf) // 20):
            h, doff, dsz = struct.unpack_from("<QQI", buf, i * 20)
            if h == want:
                return doff, dsz
        off += n * 20
        remaining -= n
    return None


def extract(chunk0_path=None):
    path = chunk0_path or os.path.join(RUNTIME, "chunk0.rpkg")
    os.makedirs(OUT_DIR, exist_ok=True)
    f, hc, tbase = open_rpkg(path)
    results = {}
    try:
        for cid, h in TARGETS.items():
            loc = find_hash(f, hc, tbase, h)
            if not loc:
                print(f"{cid}: NOT FOUND")
                continue
            doff, dsz = loc
            stored = dsz & 0x3FFFFFFF
            xored = bool(dsz & 0x80000000)
            if stored == 0:
                print(f"{cid}: raw resource unsupported")
                continue
            f.seek(doff)
            raw = bytearray(f.read(stored))
            if xored:
                for i in range(len(raw)):
                    raw[i] ^= XOR_KEY[i % 8]
            # Output size: decompress with a generous cap via trial sizes is
            # not needed - resource table gives size_final, but for these
            # JSONs lz4 can decode into a large buffer with expected_size.
            out = lz4.block.decompress(bytes(raw), uncompressed_size=1024 * 512)
            j = json.loads(out.decode("utf-8"))
            dest = os.path.join(OUT_DIR, cid + ".json")
            with open(dest, "wb") as g:
                g.write(out)
            print(f"{cid}: {len(out)} bytes -> {dest} "
                  f"codename={j['Metadata'].get('CodeName_Hint')}")
            results[cid] = out
    finally:
        f.close()
    return results


if __name__ == "__main__":
    extract(sys.argv[1] if len(sys.argv) > 1 else None)
