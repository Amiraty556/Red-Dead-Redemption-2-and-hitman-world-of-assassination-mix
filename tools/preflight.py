#!/usr/bin/env python3
"""Preflight: the sheets are the source of truth, this proves they are buildable.

Two modes:
  --allow-unverified   (build gate) every cell filled, every reference resolves.
                       unverified cells allowed but listed.
  (default, strict)    (release gate) additionally every 'verified' cell must be
                       true and presentInInstall must be true where declared.

Exit 0 = clean. Exit 1 = list of findings (fix the sheets, not the code).
"""
import json, os, re, sys

SHEETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sheets")
ALLOW_NONE = {"allow-unverified"} if "--allow-unverified" in sys.argv else set()
STRICT = "--allow-unverified" not in sys.argv

findings = []
unverified = []

# Cells whose value is allowed to be null/[] BY DECLARATION (not oversight):
# every one of these means "this row has none of this", and is written as such.
ALLOW_EMPTY = {
    "contracts": {"vehicleRef"},
    "targets": {"nameFromContract", "weaponRef"},
    "systems": {"natives", "dependsOn"},
    "natives": {"params"},
    "weapons": {"weaponRef"},
    "gadgets": {"weaponRef"},
    "hitman_content": {"ioiHash"},
}


def load(name):
    with open(os.path.join(SHEETS, name), encoding="utf-8") as f:
        return json.load(f)


def cell_empty(v):
    if v is None:
        return True
    if isinstance(v, str) and v.strip() == "":
        return True
    if isinstance(v, list) and len(v) == 0:
        return True
    return False


sheets = {}
for name in ["contracts", "targets", "weapons", "gadgets", "systems", "hooks", "natives", "hitman_content"]:
    try:
        data = load(name + ".json")
    except FileNotFoundError:
        findings.append(f"FATAL: sheet missing: {name}.json")
        continue
    if isinstance(data, list):
        # natives.json is generated as a bare list; normalize to columns/rows
        cols = sorted({k for r in data for k in r})
        data = {"columns": cols, "rows": data}
    sheets[name] = data

if findings:
    print("\n".join(findings)); sys.exit(1)

# ---- 1. every cell filled -------------------------------------------------
for name, sheet in sheets.items():
    cols = sheet.get("columns")
    rows = sheet.get("rows")
    if not cols or not rows:
        findings.append(f"{name}: missing columns or rows")
        continue
    for i, row in enumerate(rows):
        rid = row.get("id", f"row{i}")
        for c in cols:
            if c not in row:
                findings.append(f"{name}/{rid}: COLUMN '{c}' absent (crossing unfilled)")
            elif cell_empty(row[c]) and c not in ALLOW_EMPTY.get(name, set()):
                findings.append(f"{name}/{rid}: cell '{c}' empty")
        for k in row:
            if k not in cols and k not in ("outfitCandidates",):
                findings.append(f"{name}/{rid}: field '{k}' not in columns (sheet drift)")

# ---- 2. reference resolution ---------------------------------------------
by_id = {name: {r.get("id"): r for r in s.get("rows", [])}
         for name, s in sheets.items()}

natives_names = set(r["name"] for r in sheets["natives"]["rows"])
weapon_ids = set(by_id["weapons"])
gadget_ids = set(by_id["gadgets"])
contract_ids = set(by_id["contracts"])
target_ids = set(by_id["targets"])
system_ids = set(by_id["systems"])
hc_ids = set(by_id["hitman_content"])

# contracts -> targets, hitman content, vehicles
veh_ids = set()
for c in sheets["contracts"]["rows"]:
    if c.get("vehicleRef"):
        veh_ids.add(c["vehicleRef"])
for c in sheets["contracts"]["rows"]:
    for t in c.get("targetRefs", []):
        if t not in target_ids:
            findings.append(f"contracts/{c['id']}: targetRefs '{t}' does not resolve in targets.json")
    if c.get("hitmanContractRef") not in hc_ids:
        findings.append(f"contracts/{c['id']}: hitmanContractRef '{c.get('hitmanContractRef')}' not in hitman_content.json")

# targets -> contracts, weapons
for t in sheets["targets"]["rows"]:
    if t.get("contractRef") not in contract_ids:
        findings.append(f"targets/{t['id']}: contractRef '{t.get('contractRef')}' does not resolve")
    wr = t.get("weaponRef")
    if wr and wr not in weapon_ids:
        findings.append(f"targets/{t['id']}: weaponRef '{wr}' does not resolve in weapons.json")

# weapons -> systems
for w in sheets["weapons"]["rows"]:
    if w.get("system") not in system_ids:
        findings.append(f"weapons/{w['id']}: system '{w.get('system')}' does not resolve in systems.json")

# gadgets -> weapons + systems
for g in sheets["gadgets"]["rows"]:
    wr = g.get("weaponRef")
    if wr and wr not in weapon_ids:
        findings.append(f"gadgets/{g['id']}: weaponRef '{wr}' does not resolve")
    for s in g.get("systems", []):
        if s not in system_ids:
            findings.append(f"gadgets/{g['id']}: system '{s}' does not resolve")

# systems -> natives, dependsOn, hitman rows
for s in sheets["systems"]["rows"]:
    for n in s.get("natives", []):
        if n not in natives_names:
            findings.append(f"systems/{s['id']}: native '{n}' not present in natives.json")
    for d in s.get("dependsOn", []):
        if d not in system_ids:
            findings.append(f"systems/{s['id']}: dependsOn '{d}' does not resolve")

# natives: hash shape
for n in sheets["natives"]["rows"]:
    h = n.get("hash", "")
    if not re.match(r"^0x[0-9A-F]{16}$", h):
        findings.append(f"natives/{n.get('name')}: hash '{h}' not 0x + 16 hex")

# hooks: pattern shape
for h in sheets["hooks"]["rows"]:
    p = h.get("pattern", "")
    if not re.match(r"^([0-9A-F]{2}|\?)( ([0-9A-F]{2}|\?))+$", p):
        findings.append(f"hooks/{h['id']}: pattern malformed")

# hitman content: hash shape / declared presence
for h in sheets["hitman_content"]["rows"]:
    ih = h.get("ioiHash", "")
    if ih not in ("n/a",) and not re.match(r"^0x[0-9A-F]{16}$", ih) and not ih.startswith("computed"):
        findings.append(f"hitman_content/{h['id']}: ioiHash '{ih}' malformed")
    if not h.get("presentInInstall"):
        unverified.append(f"hitman_content/{h['id']}: presentInInstall=false ({h.get('purpose','')[:60]})")

# ---- 3. verified cells ----------------------------------------------------
for name in ["contracts", "targets", "weapons", "gadgets"]:
    for r in sheets[name]["rows"]:
        if r.get("verified") is False:
            unverified.append(f"{name}/{r['id']}: verified=false")
for h in sheets["hooks"]["rows"]:
    if h.get("status") != "verified":
        unverified.append(f"hooks/{h['id']}: status={h.get('status')}")

# ---- report ---------------------------------------------------------------
print(f"preflight ({'build gate, unverified allowed' if not STRICT else 'RELEASE gate, strict'}):")
print(f"  sheets: {len(sheets)}  rows: {sum(len(s['rows']) for s in sheets.values())}")
if unverified:
    print(f"  unverified cells ({len(unverified)}):")
    for u in unverified:
        print("    -", u)
if findings:
    print(f"  FINDINGS ({len(findings)}):")
    for f in findings:
        print("    !", f)
    sys.exit(1)
if STRICT and unverified:
    print("  ! strict mode: unverified cells block release")
    sys.exit(1)
print("  CLEAN - build may proceed" + ("" if not STRICT else " - release allowed"))
sys.exit(0)
