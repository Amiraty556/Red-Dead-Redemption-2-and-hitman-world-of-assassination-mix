// ArthurContracts - host selftest for the runtime HITMAN reader.
// Extracts the three contract JSONs via src/rpkg.hpp + src/hitman.hpp and
// compares them byte-for-byte against the Python reference extraction
// (tools/extract_hitman_contracts.py output in ~/hm-extract/*.json).
// Run: build/selftest.exe [reference_dir]   (default: $HOME/hm-extract)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../src/hitman.hpp"

static std::string ReadAll(const std::string& p) {
    return ac::ReadWholeFile(p);
}

int main(int argc, char** argv) {
    std::string refDir;
    if (argc > 1) refDir = argv[1];
    else {
        char home[MAX_PATH] = {0};
        DWORD n = GetEnvironmentVariableA("USERPROFILE", home, MAX_PATH);
        refDir = std::string(home, n) + "\\hm-extract";
    }

    ac::HitmanContract cs[3];
    ac::LoadContracts(cs);

    int failures = 0;
    for (auto& c : cs) {
        if (!c.ok) {
            printf("FAIL %-11s not ok: %s\n", c.id, c.err.c_str());
            failures++;
            continue;
        }
        std::string ref = ReadAll(refDir + "\\" + c.id + ".json");
        if (ref.empty()) {
            printf("FAIL %-11s reference missing: %s\n", c.id, refDir.c_str());
            failures++;
            continue;
        }
        bool same = ref == c.rawJson;
        printf("%s %-11s cpp=%zu ref=%zu codename=%s objs=%zu\n",
               same ? "PASS" : "FAIL", c.id, c.rawJson.size(), ref.size(),
               c.codename.c_str(), c.objectives.size());
        if (!same) failures++;
        // Also require the parsed fields to be non-trivial.
        if (c.codename.empty() || c.objectives.empty()) {
            printf("FAIL %-11s parsed fields empty\n", c.id);
            failures++;
        }
        if (c.objectives.size() > 0)
            printf("      obj[0]=%s\n", c.objectives[0].c_str());
    }
    printf(failures ? "SELFTEST FAILED (%d)\n" : "SELFTEST OK\n", failures);
    return failures ? 1 : 0;
}
