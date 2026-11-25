"""Writes the enemy randomizer's native candidates of each host as JSON for fleet-test --enemy-walk.

Ship: kNativeRows of EnemyPlacement.cpp that have an enhancement CVar, minus kSupersededNatives.
2Ship: sNativeEnemies of EnemyRandomizer.cpp without ENEMY_ORIGINAL_ONLY or ENEMY_NEVER_REPLACED.
Mod enemies are not listed here: the enemy_spawn_tester mod publishes them from the ActorRegistry.
"""

import argparse
import json
import re
from pathlib import Path

ENEMY_NOT_IN_CLEAR_ROOMS = 1 << 1
ENEMY_NOT_IN_TIMED_ROOMS = 1 << 2
ENEMY_MINIBOSS = 1 << 3
ENEMY_NEEDS_WATER = 1 << 4
ENEMY_NOT_IN_WATER = 1 << 5
ENEMY_NEEDS_CEILING = 1 << 6
ENEMY_UNDERWATER = 1 << 7
SURFACE_WALKER = 1 << 16
MM_ORIGINAL_ONLY = 1 << 17
MM_NEVER_REPLACED = 1 << 18

OOT_FLAGS = {
    "0": 0,
    "MINIBOSS": ENEMY_MINIBOSS,
    "UNDERWATER": ENEMY_UNDERWATER,
    "SURFACE": ENEMY_NEEDS_WATER,
    "AQUATIC": ENEMY_NEEDS_WATER | ENEMY_UNDERWATER,
    "ENEMY_SURFACE_WALKER": SURFACE_WALKER,
    "SOH_ACTOR_ENEMY_MINIBOSS": ENEMY_MINIBOSS,
    "SOH_ACTOR_ENEMY_NOT_IN_CLEAR_ROOMS": ENEMY_NOT_IN_CLEAR_ROOMS,
    "SOH_ACTOR_ENEMY_NOT_IN_TIMED_ROOMS": ENEMY_NOT_IN_TIMED_ROOMS,
    "SOH_ACTOR_ENEMY_NEEDS_WATER": ENEMY_NEEDS_WATER,
    "SOH_ACTOR_ENEMY_NOT_IN_WATER": ENEMY_NOT_IN_WATER,
    "SOH_ACTOR_ENEMY_NEEDS_CEILING": ENEMY_NEEDS_CEILING,
}
MM_FLAGS = {
    "0": 0,
    "MINIBOSS": ENEMY_MINIBOSS,
    "NOT_CLEAR": ENEMY_NOT_IN_CLEAR_ROOMS,
    "NOT_TIMED": ENEMY_NOT_IN_TIMED_ROOMS,
    "SURFACE": ENEMY_NEEDS_WATER,
    "UNDERWATER": ENEMY_UNDERWATER,
    "AQUATIC": ENEMY_NEEDS_WATER | ENEMY_UNDERWATER,
    "ENEMY_SURFACE_WALKER": SURFACE_WALKER,
    "CEILING": ENEMY_NEEDS_CEILING,
    "ENEMY_ORIGINAL_ONLY": MM_ORIGINAL_ONLY,
    "ENEMY_NEVER_REPLACED": MM_NEVER_REPLACED,
}


def read_actor_ids(table_path):
    ids = {}
    pattern = re.compile(r"/\*\s*(0x[0-9A-Fa-f]+)\s*\*/\s*DEFINE_ACTOR\w*\(\s*\w+\s*,\s*(ACTOR_\w+)")
    for line in table_path.read_text(encoding="utf-8").splitlines():
        match = pattern.search(line)
        if match:
            ids[match.group(2)] = int(match.group(1), 16)
    return ids


def split_fields(row):
    fields = []
    depth = 0
    current = ""
    for char in row:
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        if char == "," and depth == 0:
            fields.append(current.strip())
            current = ""
        else:
            current += char
    fields.append(current.strip())
    return fields


def parse_int(text):
    cast = re.fullmatch(r"static_cast<int16_t>\((.+)\)", text)
    value = int(cast.group(1) if cast else text, 0)
    return value - 0x10000 if value >= 0x8000 else value


def parse_flags(text, names):
    flags = 0
    for token in text.split("|"):
        flags |= names[token.strip()]
    return flags


def read_rows(source, table_name):
    block = re.search(table_name + r"\[\]\s*=\s*\{(.*?)\n\};", source, re.S).group(1)
    return [split_fields(row) for row in re.findall(r"^\s*\{\s*(\"[^\n]*?)\s*\},?\s*$", block, re.M)]


def extract_oot(root):
    source = (root / "soh/soh/Enhancements/randomizer/EnemyPlacement.cpp").read_text(encoding="utf-8")
    actor_ids = read_actor_ids(root / "soh/include/tables/actor_table.h")
    superseded = re.findall(r'\{\s*"([\w.]+)"\s*,\s*"([\w.]+)"\s*\}', re.search(
        r"kSupersededNatives\[\]\s*=\s*\{(.*?)\n\};", source, re.S).group(1))
    candidates = []
    for key, name, actor, params, _logic, _soul, flags, cvar in read_rows(source, "kNativeRows"):
        if cvar == "nullptr":
            continue
        candidates.append({"key": key.strip('"'), "name": name.strip('"'), "actorId": actor_ids[actor],
                           "params": parse_int(params), "spawnHeight": 0, "rotX": 0, "rotZ": 0,
                           "flags": parse_flags(flags, OOT_FLAGS)})
    return {"game": "oot", "candidates": candidates,
            "superseded": [{"native": native, "mod": mod} for native, mod in superseded]}


def extract_mm(root):
    source = (root / "mm/2s2h/Enhancements/Modes/EnemyRandomizer.cpp").read_text(encoding="utf-8")
    actor_ids = read_actor_ids(root / "mm/include/tables/actor_table.h")
    candidates = []
    for key, name, actor, _mask, _value, params, _soul, flags, height, rot_x, rot_z in read_rows(
            source, "sNativeEnemies"):
        flag_bits = parse_flags(flags, MM_FLAGS)
        if flag_bits & (MM_ORIGINAL_ONLY | MM_NEVER_REPLACED):
            continue
        candidates.append({"key": key.strip('"'), "name": name.strip('"'), "actorId": actor_ids[actor],
                           "params": parse_int(params), "spawnHeight": int(height, 0), "rotX": int(rot_x, 0),
                           "rotZ": int(rot_z, 0), "flags": flag_bits})
    return {"game": "mm", "candidates": candidates, "superseded": []}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--oot-root", type=Path, required=True)
    parser.add_argument("--mm-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path(__file__).parent)
    args = parser.parse_args()
    for document in (extract_oot(args.oot_root), extract_mm(args.mm_root)):
        path = args.out / f"EnemyCandidates-{document['game']}.json"
        path.write_text(json.dumps(document, indent=1), encoding="utf-8")
        print(f"{path}: {len(document['candidates'])} candidates")


if __name__ == "__main__":
    main()
