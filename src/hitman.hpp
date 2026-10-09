// ArthurContracts - HITMAN install discovery + contract data extraction.
//
// Reads the player's OWN HITMAN install at runtime (Melty rule: HITMAN is a
// "secondary" game, found via Steam libraryfolders.vdf; nothing shipped).
// Data needed for the v1 dossier (sheets/hitman_content.json):
//   - Metadata: CodeName_Hint, Title, Location (loc keys kept verbatim)
//   - Objectives: _comment strings ("Eliminate Hush" - plain English)
// Fallback contract text is our own (sheets/contracts.json briefLine values,
// embedded in main.cpp - our MIT text, never game files).
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "log.hpp"
#include "rpkg.hpp"

namespace ac {

struct HitmanContract {
    const char* id = "";       // sheet row id (hc_rat ...)
    uint64_t hash = 0;
    bool ok = false;
    std::string err;
    std::string rawJson;       // decoded resource (for selftest + later features)
    std::string codename;      // Metadata.CodeName_Hint
    std::string titleKey;      // Metadata.Title (localization key)
    std::string locationKey;   // Metadata.Location (localization key)
    std::vector<std::string> objectives;  // _comment strings, cleaned
};

// ---------------------------------------------------------------------------
// Targeted JSON scanners. These are deliberately not a full parser: the
// contract files are machine-generated single-line JSON; we only need
// well-known keys. Brace matching is string/escape aware.
// ---------------------------------------------------------------------------

// Returns position of `"key"` followed by ':' , or npos.
inline size_t FindJsonKey(const std::string& j, const char* key, size_t from) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = j.find(pat, from);
    while (p != std::string::npos) {
        size_t q = p + pat.size();
        while (q < j.size() && (j[q] == ' ' || j[q] == '\t' || j[q] == '\n' || j[q] == '\r')) q++;
        if (q < j.size() && j[q] == ':') return p;
        p = j.find(pat, p + 1);
    }
    return std::string::npos;
}

// Parse a JSON string starting at j[pos] == '"'. Returns end position after
// the closing quote, fills out (handles common escapes + \uXXXX BMP).
inline size_t ParseJsonString(const std::string& j, size_t pos, std::string& out) {
    out.clear();
    if (pos >= j.size() || j[pos] != '"') return std::string::npos;
    pos++;
    auto put = [](std::string& s, unsigned cp) {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) {
            s += (char)(0xC0 | (cp >> 6));
            s += (char)(0x80 | (cp & 0x3F));
        } else {
            s += (char)(0xE0 | (cp >> 12));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
    };
    while (pos < j.size()) {
        char c = j[pos++];
        if (c == '"') return pos;
        if (c != '\\') { out += c; continue; }
        if (pos >= j.size()) return std::string::npos;
        char e = j[pos++];
        switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (pos + 4 > j.size()) return std::string::npos;
                unsigned cp = 0;
                for (int i = 0; i < 4; i++) {
                    char h = j[pos++];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                    else return std::string::npos;
                }
                put(out, cp);
                break;
            }
            default: return std::string::npos;
        }
    }
    return std::string::npos;
}

// First string value for `"key": "..."` at/after `from`.
inline bool JsonStringValue(const std::string& j, const char* key, size_t from,
                            std::string& out) {
    size_t k = FindJsonKey(j, key, from);
    if (k == std::string::npos) return false;
    size_t colon = j.find(':', k);
    size_t v = j.find_first_not_of(" \t\r\n", colon + 1);
    if (v == std::string::npos || j[v] != '"') return false;
    return ParseJsonString(j, v, out) != std::string::npos;
}

// Span of the object/array value of `"key"` (brace matched). npos on failure.
inline size_t JsonValueSpan(const std::string& j, const char* key, size_t from,
                            size_t& endOut) {
    size_t k = FindJsonKey(j, key, from);
    if (k == std::string::npos) return std::string::npos;
    size_t colon = j.find(':', k);
    size_t v = j.find_first_not_of(" \t\r\n", colon + 1);
    if (v == std::string::npos) return std::string::npos;
    char c = j[v];
    if (c != '{' && c != '[') return std::string::npos;
    int depth = 0;
    bool inStr = false, esc = false;
    for (size_t i = v; i < j.size(); i++) {
        char ch = j[i];
        if (inStr) {
            if (esc) esc = false;
            else if (ch == '\\') esc = true;
            else if (ch == '"') inStr = false;
            continue;
        }
        if (ch == '"') { inStr = true; continue; }
        if (ch == '{' || ch == '[') depth++;
        else if (ch == '}' || ch == ']') {
            depth--;
            if (depth == 0) { endOut = i + 1; return v; }
        }
    }
    return std::string::npos;
}

// All string values of every `"key": "..."` in the document.
inline void JsonAllStringValues(const std::string& j, const char* key,
                                std::vector<std::string>& out) {
    size_t from = 0;
    std::string pat = std::string("\"") + key + "\"";
    while (true) {
        size_t p = j.find(pat, from);
        if (p == std::string::npos) break;
        size_t q = p + pat.size();
        while (q < j.size() && (j[q] == ' ' || j[q] == '\t')) q++;
        if (q < j.size() && j[q] == ':') {
            size_t v = j.find_first_not_of(" \t\r\n", q + 1);
            if (v != std::string::npos && j[v] == '"') {
                std::string s;
                size_t end = ParseJsonString(j, v, s);
                if (end != std::string::npos && !s.empty()) out.push_back(s);
                from = end == std::string::npos ? q + 1 : end;
                continue;
            }
        }
        from = p + 1;
    }
}

// "_comment" values look like "----- Eliminate Hush -----" - strip the dashes.
inline std::string CleanObjective(const std::string& s) {
    size_t a = s.find_first_not_of('-');
    if (a == std::string::npos) return s;
    size_t b = s.find_last_not_of('-');
    std::string t = s.substr(a, b - a + 1);
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
    return t;
}

// ---------------------------------------------------------------------------
// Install discovery (sheet hc_steam_discovery + hc_missing_notice fallback).
// ---------------------------------------------------------------------------

inline bool FileExists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool DirExists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// A candidate root qualifies if <root>\steamapps\common\HITMAN 3\runtime\chunk0.rpkg exists.
inline std::string HitmanIn(const std::string& root) {
    std::string c = root;
    if (!c.empty() && c.back() != '\\' && c.back() != '/') c += '\\';
    std::string h = c + "steamapps\\common\\HITMAN 3";
    if (FileExists(h + "\\runtime\\chunk0.rpkg")) return h;
    return "";
}

// Extract "path" values from a libraryfolders.vdf (old and new formats both
// use "path" "<escaped>" pairs).
inline void VdfPaths(const std::string& vdf, std::vector<std::string>& out) {
    size_t from = 0;
    while (true) {
        size_t p = vdf.find("\"path\"", from);
        if (p == std::string::npos) break;
        size_t q = vdf.find('"', p + 6);
        if (q == std::string::npos) break;
        size_t e = vdf.find('"', q + 1);
        if (e == std::string::npos) break;
        std::string v = vdf.substr(q + 1, e - q - 1);
        std::string u;
        for (size_t i = 0; i < v.size(); i++) {
            if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == '\\') { u += '\\'; i++; }
            else u += v[i];
        }
        out.push_back(u);
        from = e + 1;
    }
}

inline std::string ReadWholeFile(const std::string& p) {
    HANDLE f = CreateFileA(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return "";
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(f);
        return "";
    }
    std::string s((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    BOOL ok = ReadFile(f, &s[0], (DWORD)s.size(), &got, nullptr);
    CloseHandle(f);
    if (!ok) return "";
    s.resize(got);
    return s;
}

inline std::string FindHitmanInstall() {
    // Candidate library roots in sheet order (explicit fallback guesses first).
    std::vector<std::string> roots = {"D:\\SteamLibrary"};

    char buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("ProgramFiles(x86)", buf, MAX_PATH);
    if (n && n < MAX_PATH) roots.push_back(std::string(buf) + "\\Steam");
    n = GetEnvironmentVariableA("ProgramFiles", buf, MAX_PATH);
    if (n && n < MAX_PATH) roots.push_back(std::string(buf) + "\\Steam");

    // Try direct placements, then each root's libraryfolders.vdf discoveries.
    for (size_t i = 0; i < roots.size(); i++) {
        std::string h = HitmanIn(roots[i]);
        if (!h.empty()) return h;
    }
    for (const auto& root : roots) {
        std::string vdfPath = root + "\\steamapps\\libraryfolders.vdf";
        if (!FileExists(vdfPath)) continue;
        std::string vdf = ReadWholeFile(vdfPath);
        if (vdf.empty()) continue;
        std::vector<std::string> libs;
        VdfPaths(vdf, libs);
        for (const auto& lib : libs) {
            std::string h = HitmanIn(lib);
            if (!h.empty()) return h;
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// Load the three contract resources from the player's install.
// ---------------------------------------------------------------------------

inline void LoadContracts(HitmanContract out[3]) {
    static const struct { const char* id; uint64_t hash; } kRows[3] = {
        {"hc_bulldog", 0x00F1C98E21AC5D76ull},
        {"hc_llama", 0x00C27E02CD7CD328ull},
        {"hc_rat", 0x0092633EE2D15990ull},
    };

    ULONGLONG t0 = GetTickCount64();
    std::string install = FindHitmanInstall();
    if (install.empty()) {
        for (int i = 0; i < 3; i++) {
            out[i].id = kRows[i].id;
            out[i].hash = kRows[i].hash;
            out[i].err = "HITMAN_NOT_FOUND";
        }
        AC_LOG("hitman: install not found (searched libraryfolders.vdf + guesses)");
        return;
    }
    AC_LOG("hitman: install = %s", install.c_str());

    Rpkg rpkg;
    std::string err;
    std::string chunk = install + "\\runtime\\chunk0.rpkg";
    if (!rpkg.Open(chunk, err)) {
        AC_LOG("hitman: open failed path=%s err=%s", chunk.c_str(), err.c_str());
        for (int i = 0; i < 3; i++) {
            out[i].id = kRows[i].id;
            out[i].hash = kRows[i].hash;
            out[i].err = "RPKG_OPEN_FAILED: " + err;
        }
        return;
    }
    AC_LOG("hitman: chunk0 open ok entries=%zu in %llums", rpkg.Count(),
           GetTickCount64() - t0);

    for (int i = 0; i < 3; i++) {
        auto& c = out[i];
        c.id = kRows[i].id;
        c.hash = kRows[i].hash;
        ULONGLONG t1 = GetTickCount64();
        std::vector<char> buf;
        if (!rpkg.Extract(c.hash, buf, 512 * 1024, err)) {
            c.err = "EXTRACT_FAILED: " + err;
            AC_LOG("hitman: %s extract FAILED %s", c.id, c.err.c_str());
            continue;
        }
        c.rawJson.assign(buf.data(), buf.size());
        // Metadata span -> keys inside it (Metadata may not be first: llama
        // starts with Data, so scope the search by brace span).
        size_t mEnd = 0;
        size_t mStart = JsonValueSpan(c.rawJson, "Metadata", 0, mEnd);
        size_t searchFrom = 0, searchTo = c.rawJson.size();
        if (mStart != std::string::npos) { searchFrom = mStart; searchTo = mEnd; }
        std::string meta = c.rawJson.substr(searchFrom, searchTo - searchFrom);
        JsonStringValue(meta, "CodeName_Hint", 0, c.codename);
        JsonStringValue(meta, "Title", 0, c.titleKey);
        JsonStringValue(meta, "Location", 0, c.locationKey);

        std::vector<std::string> comments;
        JsonAllStringValues(c.rawJson, "_comment", comments);
        for (const auto& s : comments) {
            std::string t = CleanObjective(s);
            if (!t.empty()) c.objectives.push_back(t);
        }

        c.ok = !c.codename.empty() || !c.objectives.empty();
        if (!c.ok) c.err = "PARSE_EMPTY";
        AC_LOG("hitman: %s ok=%d bytes=%zu codename=%s title=%s loc=%s objs=%zu in %llums",
               c.id, (int)c.ok, c.rawJson.size(), c.codename.c_str(),
               c.titleKey.c_str(), c.locationKey.c_str(), c.objectives.size(),
               GetTickCount64() - t1);
        for (size_t k = 0; k < c.objectives.size() && k < 6; k++)
            AC_LOG("hitman:   %s obj[%zu]=%s", c.id, k, c.objectives[k].c_str());
    }
    AC_LOG("hitman: load done in %llums", GetTickCount64() - t0);
}

}  // namespace ac
