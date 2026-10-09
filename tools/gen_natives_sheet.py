#!/usr/bin/env python3
"""Generate sheets/natives.json from alloc8or's rdr3-nativedb-data.

The natives sheet is the contract between our systems (systems.json names them)
and the game. Generated once, then treated as source of truth: preflight checks
every name referenced by systems.json exists here with a hash.
"""
import json, sys, os

DB = os.path.expanduser(r"~\rdr3-nativedb-data\natives.json")
OUT = os.path.join(os.path.dirname(__file__), "..", "sheets", "natives.json")

# Every native the mashup calls, grouped by the system that uses it.
WANTED = {
    "loader_core": ["GET_GAME_TIMER", "GET_FRAME_TIME", "GET_HASH_KEY", "IS_STRING_NULL", "ARE_STRINGS_EQUAL"],
    "hud_overlay": [
        "VAR_STRING", "_DISPLAY_TEXT", "_SET_TEXT_COLOR",
        "GET_HUD_SCREEN_POSITION_FROM_WORLD_POSITION", "DISPLAY_HUD",
        "_BG_DISPLAY_TEXT", "_BG_SET_TEXT_SCALE", "_BG_SET_TEXT_COLOR",
        "_UILOG_SET_CACHED_OBJECTIVE", "_UILOG_PRINT_CACHED_OBJECTIVE",
        "_UILOG_CLEAR_CACHED_OBJECTIVE", "SET_TEXT_SCALE", "SET_TEXT_CENTRE",
        "SET_TEXT_DROPSHADOW",
    ],
    "hitman_reader": [],
    "contract_flow": [
        "PLAYER_PED_ID", "IS_ENTITY_DEAD", "SET_ENTITY_AS_MISSION_ENTITY",
        "SET_ENTITY_INVINCIBLE", "REQUEST_MODEL", "HAS_MODEL_LOADED",
        "SET_MODEL_AS_NO_LONGER_NEEDED",
    ],
    "briefing_menu": ["IS_CONTROL_PRESSED", "IS_DISABLED_CONTROL_PRESSED", "GET_CONTROL_NORMAL"],
    "spawning": [
        "CREATE_PED", "DELETE_ENTITY", "DOES_ENTITY_EXIST", "GET_ENTITY_COORDS",
        "SET_ENTITY_COORDS", "GET_ENTITY_HEADING", "SET_ENTITY_HEADING",
        "SET_ENTITY_VISIBLE", "FREEZE_ENTITY_POSITION", "SET_BLOCKING_OF_NON_TEMPORARY_EVENTS",
        "CREATE_OBJECT", "DELETE_OBJECT", "CREATE_VEHICLE", "DELETE_VEHICLE",
        "IS_VEHICLE_DRIVEABLE", "GET_PED_IN_VEHICLE_SEAT", "IS_PED_IN_ANY_VEHICLE",
        "SET_PED_DROPS_WEAPONS_WHEN_DEAD",
    ],
    "detection_instinct": [
        "IS_PED_DEAD_OR_DYING", "SET_PED_SEEING_RANGE", "SET_PED_HEARING_RANGE",
        "SET_PED_FLEE_ATTRIBUTES", "SET_PED_COMBAT_ATTRIBUTES", "SET_PED_CONFIG_FLAG",
        "START_SHAPE_TEST_LOS_PROBE", "GET_SHAPE_TEST_RESULT",
        "CLEAR_PED_TASKS", "IS_PED_STOPPED", "IS_PED_SHOOTING",
    ],
    "guard_ai": [
        "TASK_GO_STRAIGHT_TO_COORD", "TASK_TURN_PED_TO_FACE_ENTITY", "TASK_STAND_STILL",
        "TASK_COMBAT_PED", "TASK_FOLLOW_NAV_MESH_TO_COORD", "TASK_VEHICLE_DRIVE_WANDER",
    ],
    "weapons": [
        "GIVE_WEAPON_TO_PED", "HAS_PED_GOT_WEAPON", "SET_CURRENT_PED_WEAPON",
        "GET_AMMO_IN_PED_WEAPON", "SET_AMMO_IN_CLIP",
    ],
    "kills": [
        "APPLY_DAMAGE_TO_PED", "SET_PED_TO_RAGDOLL", "IS_PED_RAGDOLL",
        "GET_PED_SOURCE_OF_DEATH", "GET_ENTITY_HEALTH", "SET_ENTITY_HEALTH",
        "IS_PED_A_PLAYER",
    ],
    "disguise": [
        "_EQUIP_META_PED_OUTFIT", "_REQUEST_META_PED_OUTFIT",
        "_HAS_META_PED_OUTFIT_LOADED", "_RELEASE_META_PED_OUTFIT_REQUEST",
    ],
    "target_marker": [
        "BLIP_ADD_FOR_ENTITY", "SET_BLIP_SPRITE", "SET_BLIP_SCALE",
        "SET_BLIP_COORDS", "REMOVE_BLIP",
    ],
    "sound_distract": ["PLAY_SOUND_FROM_ENTITY", "_STOP_SOUND_WITH_NAME", "_STOP_SOUND_WITH_ID"],
    "scoring": [],
}


# Natives missing from alloc8or's named set, taken from the ScriptHook-style
# public reference header (Device9999/RDR2-Invoker-Entrypoint-Dumper, b1207),
# hash-verified against the same natives that alloc8or does name.
EXTRA = {
    "SET_TEXT_SCALE": ["HUD", "0x4170B650590B3B00", "void", ["float scale", "float size"]],
    "SET_TEXT_CENTRE": ["HUD", "0xBE5261939FBECB8C", "void", ["BOOL align"]],
    "SET_TEXT_DROPSHADOW": ["HUD", "0x1BE39DBAA7263CA5", "void", ["int distance", "int r", "int g", "int b", "int a"]],
}


def main():
    db = json.load(open(DB, encoding="utf-8"))
    by_name = {}
    for ns, funcs in db.items():
        for h, f in funcs.items():
            n = f.get("name", "")
            if not n or n.startswith("_0x"):
                continue
            # Prefer entries with real names; keep first unless it has params info
            by_name.setdefault(n, (ns, h, f))
    rows = []
    missing = []
    for system, names in WANTED.items():
        for n in names:
            if n in by_name:
                ns, h, f = by_name[n]
                rows.append({
                    "name": n,
                    "namespace": ns,
                    "hash": h if h.startswith("0x") else "0x" + h,
                    "returnType": f.get("return_type", "void"),
                    "params": [f"{p.get('type','Any')} {p.get('name','')}".strip()
                               for p in f.get("params", [])],
                    "usedBy": system,
                    "source": "alloc8or/rdr3-nativedb-data",
                })
            elif n in EXTRA:
                ns, h, rt, pr = EXTRA[n]
                rows.append({
                    "name": n, "namespace": ns, "hash": h, "returnType": rt,
                    "params": pr, "usedBy": system,
                    "source": "Device9999/RDR2-Invoker-Entrypoint-Dumper natives.hpp (public)",
                })
            else:
                missing.append(f"{system}:{n}")
    # de-dupe by name (keep first, merge usedBy)
    merged = {}
    for r in rows:
        if r["name"] in merged:
            if r["usedBy"] not in merged[r["name"]]["usedBy"].split("+"):
                merged[r["name"]]["usedBy"] += "+" + r["usedBy"]
        else:
            r = dict(r)
            merged[r["name"]] = r
    out = sorted(merged.values(), key=lambda r: (r["namespace"], r["name"]))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    json.dump(out, open(OUT, "w", encoding="utf-8"), indent=1)
    print(f"wrote {len(out)} natives -> {os.path.normpath(OUT)}")
    if missing:
        print("MISSING (fix before preflight):")
        for m in missing:
            print("  ", m)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
