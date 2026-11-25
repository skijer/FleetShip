"""Runs fleet-test --enemy-walk split over N isolated copies of the Fleet install and merges the results.

Each copy is a private Fleet + Ship + 2Ship tree, so the walk never locks the real binaries and the shards never
share config files. Shards take every N-th request of the same deterministic list (--enemy-walk-shard i/N).

    python run_enemy_walk_shards.py --game mm --shards 8
"""

import argparse
import collections
import datetime
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

FLEET_ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(__file__).resolve().parent
SKIPPED_SUFFIXES = {".pdb", ".lib", ".exp", ".ilk", ".obj"}
SKIPPED_FOLDERS = {"logs", "saves", "Save", "x64"}
GAME_FOLDERS = ("Ship", "2Ship", "mods")
GAME_CONFIG = {"oot": "Ship/shipofharkinian.json", "mm": "2Ship/2ship2harkinian.json"}
SPAWN_TABLE = {
    "oot": "soh/soh/Enhancements/randomizer/StaticData/EnemySpawns.json",
    "mm": "mm/2s2h/Rando/StaticData/EnemySpawns.json",
}
JOYSTICK_ENV = {
    "SDL_JOYSTICK_HIDAPI": "0",
    "SDL_JOYSTICK_RAWINPUT": "0",
    "SDL_JOYSTICK_WGI": "0",
    "SDL_XINPUT_ENABLED": "0",
    "SDL_DIRECTINPUT_ENABLED": "0",
}


def is_current(source, target):
    if not target.exists():
        return False
    source_stat = source.stat()
    target_stat = target.stat()
    return source_stat.st_size == target_stat.st_size and int(source_stat.st_mtime) == int(target_stat.st_mtime)


def copy_tree(source, target):
    for entry in source.iterdir():
        if entry.name in SKIPPED_FOLDERS or entry.suffix in SKIPPED_SUFFIXES:
            continue
        destination = target / entry.name
        if entry.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
            copy_tree(entry, destination)
        elif not is_current(entry, destination):
            shutil.copy2(entry, destination)


# Ship and 2Ship are junctions to the game builds; iterdir follows them, so the copy holds real files.
def refresh_copy(install, copy):
    copy.mkdir(parents=True, exist_ok=True)
    for entry in install.iterdir():
        if entry.is_file() and entry.suffix not in SKIPPED_SUFFIXES and not is_current(entry, copy / entry.name):
            shutil.copy2(entry, copy / entry.name)
    for folder in GAME_FOLDERS:
        if (install / folder).is_dir():
            (copy / folder).mkdir(exist_ok=True)
            copy_tree(install / folder, copy / folder)


def set_tester_speed(copy, game, speed):
    path = copy / GAME_CONFIG[game]
    config = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {"CVars": {}}
    tester = config.setdefault("CVars", {}).setdefault("gMods", {}).get("EnemySpawnTester") or {}
    tester["Speed"] = speed
    config["CVars"]["gMods"]["EnemySpawnTester"] = tester
    path.write_text(json.dumps(config, indent=4), encoding="utf-8")


def read_host_roots():
    local = json.loads((FLEET_ROOT / "fleet.local.json").read_text(encoding="utf-8"))
    return {"oot": (FLEET_ROOT / local["oot"]).resolve(), "mm": (FLEET_ROOT / local["mm"]).resolve()}


def build_command(args, shard, copy, session):
    command = [str(copy / "fleet-test.exe"), "--oot", "Ship", "--mm", "2Ship", "--seed", str(args.seed),
               "--session", str(session), "--slot", "0", "--smoke", "120", "--hidden", "1",
               "--enemy-walk", args.game, "--enemy-walk-scenes", str(args.scenes),
               "--enemy-walk-frames", str(args.frames), "--enemy-walk-shard", f"{shard}/{args.shards}",
               "--enemy-walk-spawns", str(args.spawns), "--enemy-walk-candidates", str(args.candidates)]
    if args.only:
        command += ["--enemy-walk-only", args.only]
    return command


def start_shards(args):
    environment = dict(os.environ, **JOYSTICK_ENV)
    shards = []
    for shard in range(1, args.shards + 1):
        copy = args.work / f"copy{shard}"
        session = args.work / "sessions" / f"{args.game}-{shard}-{args.stamp}"
        log = open(args.work / f"shard-{args.game}-{shard}.log", "w", encoding="utf-8")
        process = subprocess.Popen(build_command(args, shard, copy, session), cwd=copy, env=environment,
                                   stdout=log, stderr=subprocess.STDOUT)
        shards.append((shard, copy, process, log))
        print(f"shard {shard}/{args.shards} started (pid {process.pid})", flush=True)
    return shards


# Each run keeps its own folder, so a partial rerun never replaces the results of a full walk.
def merge_results(args, shards):
    run = args.work / f"run-{args.game}-{args.stamp}"
    run.mkdir(parents=True)
    merged_log = run / f"fleet-enemy-walk-{args.game}-merged.jsonl"
    captures = run / "captures"
    counts = collections.Counter()
    with open(merged_log, "w", encoding="utf-8") as merged:
        for shard, copy, _, _ in shards:
            log = copy / "logs" / f"fleet-enemy-walk-{args.game}.jsonl"
            images = copy / "logs" / f"fleet-enemy-walk-{args.game}"
            if images.is_dir():
                shutil.copytree(images, captures / f"shard{shard}")
            if not log.exists():
                continue
            for line in log.read_text(encoding="utf-8").splitlines():
                record = json.loads(line)
                if "state" not in record:
                    continue
                record["shard"] = shard
                if record.get("image"):
                    record["image"] = f"shard{shard}/{record['image']}"
                result = record.get("result") or {}
                counts[(record["state"], result.get("reason") or record.get("reason", ""))] += 1
                merged.write(json.dumps(record) + "\n")
    return merged_log, counts


def is_passing(state, reason):
    return state == "ok" or (state == "unspawnable" and reason == "refused_spot")


def main():
    roots = read_host_roots()
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", choices=("oot", "mm"), required=True)
    parser.add_argument("--shards", type=int, default=8)
    parser.add_argument("--install", type=Path, default=FLEET_ROOT / "x64" / "Release")
    parser.add_argument("--work", type=Path, default=FLEET_ROOT / "build" / "enemy-walk")
    parser.add_argument("--seed", type=Path, default=TOOLS / "no-souls-vanilla.json")
    parser.add_argument("--scenes", type=int, default=4)
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--speed", type=int, default=16)
    parser.add_argument("--only", default="")
    parser.add_argument("--spawns", type=Path)
    parser.add_argument("--candidates", type=Path)
    parser.add_argument("--skip-refresh", action="store_true")
    args = parser.parse_args()
    args.spawns = (args.spawns or roots[args.game] / SPAWN_TABLE[args.game]).resolve()
    args.candidates = (args.candidates or TOOLS / f"EnemyCandidates-{args.game}.json").resolve()
    args.seed = args.seed.resolve()
    args.work = args.work.resolve()
    args.stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    (args.work / "sessions").mkdir(parents=True, exist_ok=True)
    for shard in range(1, args.shards + 1):
        copy = args.work / f"copy{shard}"
        if not args.skip_refresh:
            print(f"refreshing {copy}", flush=True)
            refresh_copy(args.install, copy)
        set_tester_speed(copy, args.game, args.speed)
    shards = start_shards(args)
    for shard, _, process, log in shards:
        process.wait()
        log.close()
        print(f"shard {shard} exited {process.returncode}", flush=True)
    merged_log, counts = merge_results(args, shards)
    failures = sum(count for (state, reason), count in counts.items() if not is_passing(state, reason))
    for (state, reason), count in counts.most_common():
        print(f"{count:5} {state} {reason}")
    print(f"merged into {merged_log}: {sum(counts.values())} results, {failures} failures")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
