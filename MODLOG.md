# MODLOG — "Arthur in HITMAN's world" mashup (RDR2 × HITMAN World of Assassination)

Journal. Anything not written here is lost at context compaction.

## The mashup (agreed with user)
- RDR2 boots (story mode, Arthur Morgan is the player). The mod turns it into HITMAN:
  three contracts of rising difficulty (street → guarded estate → moving carriage target),
  HITMAN-style dossier briefing, Instinct, disguises, silenced revolver / fiber wire / coins,
  Silent Assassin end screen, funny gadgets. Solo, story mode only.
- HITMAN WoA is NOT in Melty's catalog: role "secondary", found at runtime via Steam
  libraryfolders.vdf, its real content loaded from the player's own install. In-game
  notice when missing. Never ship HITMAN or RDR2 files.

## Environment (this PC)
- OS: Windows 10/11 (Git Bash / MSYS), user amird, no compiler preinstalled.
- Toolchain (installed by me): ~/mingw64/bin/g++.exe — winlibs GCC 16.2.0
  (MinGW-W64 x86_64-ucrt-posix-seh). Zip extracted to ~/mingw64.
- RDR2: D:\SteamLibrary\steamapps\common\Red Dead Redemption 2 (steam 1174180, 120 GB).
  Imports DINPUT8.dll, VERSION.dll, WINMM.dll, DSOUND.dll, d3d9.dll → UAL proxy works.
- HITMAN WoA: D:\SteamLibrary\steamapps\common\HITMAN 3 (steam 1659040, 83 GB).
  Retail\HITMAN3.exe, Runtime\chunk*.rpkg (Glacier RPKG). Simple Mod Framework present.
- Melty: token in env only (never write it here). RDR2 one-click loader = ultimate-asi-loader
  (auto). script-hook-rdr2 = hand-install, must NOT be a dependency.

## Route (decided 2026-10-08)
**Single native ASI (`ArthurContracts.asi`) loaded by Ultimate ASI Loader in RDR2 story mode.**

Why: Melty installs UAL by itself → one click. Script Hook RDR2 can't be depended on
(hand-install, proprietary). So the ASI is self-contained:

1. Native invocation WITHOUT ScriptHook: pattern-scan the game's own
   `GetNativeAddress(hash)` resolver — sig from emcifuntik/rdr2scripthook (MIT,
   derived from Tuxick 2019): `48 8B 15 ? ? ? ? 4C 8B C9 49 F7 D1`.
   Invoke via RAGE native Context layout (retVal/argCount/stackPtr/dataCount/…/stack[24]),
   same layout used by device9999/RDR2-Invoker-Entrypoint-Dumper (public) and ScriptHook.
2. Tick: detour `UpdateSingleScripts` (main thread, script update) —
   sig `48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 45 33 F6 BD ? ? ? ?`.
   Fallback (if sig dead on our build): hook is_dlc_present
   (`81 FA 00 00 00 00 75 0B 8A` - 0x1E, device9999 2019).
3. Active script thread: currentScriptThread global
   (`48 39 1D ? ? ? ? 75 ? 48 8D 05 ? ? ? ?`), scriptHandlerMgr
   (`48 8D 0D ? ? ? ? E8 ? ? ? ? 84 C0 75 ? 8B 8B ? ? ? ?`) — set active thread around
   native calls so thread-context natives behave.
4. Natives documented from alloc8or/rdr3-nativedb-data (public, cloned to ~/rdr3-nativedb-data).
   Generate a small header of just the natives our sheets reference.
5. HITMAN reading: own minimal RPKG parser (header → hash list → chunk entries → zlib).
   Data needed: mission/target/briefing text (JSON blobs), target photos (textures),
   audio later. zlib available in mingw. Explore locally first with glacier-modding
   RPKG-Tool (GPL — use for recon only; runtime parser is our own code).
6. HUD: in-game HUD/UI natives first (no D3D hook) — keep the ASI small. ReShade/overlay
   only if natives can't draw what we need.

License posture: our code MIT; credit Tuxick/rdr2scripthook (MIT) for technique+patterns,
MinHook (BSD-2-Clause, TsudaKageyu), alloc8or for native DB. No game files shipped.

## Hard rules from Melty/game_info
- Story mode only; say so on the listing. Never touch online, never -nobattleye,
  never touch commandline.txt. Rockstar tips-only: free single-player mod.
- Do not depend on script-hook-rdr2.

## Log of what I did (append below)
- 2026-10-08: read Melty game_info (RDR2) + search_mashups (nothing exists), search_games
  (HITMAN not in catalog, custom-hitman-world-of-assassination). Agreed design with user.
- Installed: pyyaml/pillow/numpy (um toolkit deps), winlibs GCC 16.2 (~/mingw64),
  cloned: universal-modder, RDR2-Invoker-Entrypoint-Dumper, minhook, rdr2scripthook,
  rdr3-nativedb-data.
- Verified RDR2.exe imports DINPUT8.dll (UAL proxy target).
