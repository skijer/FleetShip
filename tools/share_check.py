"""Checks that Fleet's Shared Items choices reach the seed and the games one to one.

For three launcher choices (Share All, Unshare All and a mixed one) it turns a shared.json into generator arguments
with the launcher's own code, generates the same seed number with each, and checks every seed: the families and
pairs it shares, the pairs today's mods must form (combo/tests/data/fleet_mod_pairs.json) and the copies the fill
kept. With --walk it then plays each seed hidden: every shareable item is given in one game and looked for in the
other. Run it from a copy of the install when someone is playing on the real one.
"""

import argparse
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent
GENERATION_TIMEOUT = 900
WALK_TIMEOUT = 900


def run(command, log, timeout):
    # Fleet writes its settings and logs into the working folder, so it runs from the install.
    with open(log, "w", encoding="utf-8") as output:
        return subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=timeout,
                              cwd=Path(command[0]).parent).returncode


def read_json_line(log):
    for line in Path(log).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            return json.loads(line)
    raise RuntimeError(f"{log} holds no JSON answer")


def worker(install, *arguments):
    return [str(install / "fleet.exe"), "--internal-worker", "--oot", str(install / "Ship"), "--mm",
            str(install / "2Ship"), *arguments]


def share_arguments(install, choices_file, baseline, output):
    log = output / (choices_file.stem + ".arguments.log")
    run(worker(install, "--share-arguments", str(choices_file), "--share-pairs", str(baseline)), log, 120)
    answer = read_json_line(log)
    arguments = []
    if answer["shared"]:
        arguments += ["--shared", answer["shared"]]
    if answer["unshared"]:
        arguments += ["--unshared", answer["unshared"]]
    return arguments


def list_choices(install, baseline, output):
    """The three launcher states as shared.json files, built the way the buttons leave them."""
    share_all = output / "all.choices.json"
    share_all.write_text(json.dumps({"unsharedFamilies": [], "unsharedPairs": []}), encoding="utf-8")
    options = share_arguments(install, share_all, baseline, output)
    option_keys = options[options.index("--shared") + 1].split(",") if "--shared" in options else []
    pair_keys = [pair["key"] for pair in json.loads(baseline.read_text(encoding="utf-8"))["pairs"]]
    unshare_all = output / "none.choices.json"
    unshare_all.write_text(json.dumps({"unsharedFamilies": option_keys, "unsharedPairs": [],
                                       "pendingBulkShare": False}), encoding="utf-8")
    mixed = output / "mixed.choices.json"
    mixed.write_text(json.dumps({"unsharedFamilies": option_keys[::2], "unsharedPairs": pair_keys[::2]}),
                     encoding="utf-8")
    return {"none": unshare_all, "all": share_all, "mixed": mixed}


def generate(install, settings, seed, arguments, seed_file):
    command = worker(install, "--settings", str(settings), "--seed", str(seed), "--start", "oot", *arguments,
                     "--out", str(seed_file))
    return run(command, seed_file.with_suffix(".log"), GENERATION_TIMEOUT) == 0 and seed_file.exists()


def check(install, seed_file, reference, baseline, arguments):
    report = seed_file.with_suffix(".check.json")
    command = worker(install, "--check-seed", str(seed_file), "--check-reference", str(reference),
                     "--check-baseline", str(baseline), *arguments, "--out", str(report))
    run(command, seed_file.with_suffix(".check.log"), 120)
    return json.loads(report.read_text(encoding="utf-8")) if report.exists() else {"passed": False,
                                                                                   "failures": ["no report"]}


def walk(install, seed_file, source, baseline, output):
    session = output / "sessions" / f"{seed_file.stem}-{source}-{int(time.time())}"
    command = [str(install / "fleet-test.exe"), "--oot", str(install / "Ship"), "--mm", str(install / "2Ship"),
               "--seed", str(seed_file), "--session", str(session), "--slot", "0", "--smoke", "120", "--hidden",
               "1", "--share-walk", source, "--share-walk-baseline", str(baseline)]
    status = run(command, output / f"{seed_file.stem}.walk-{source}.log", WALK_TIMEOUT)
    walk_log = install / "logs" / f"fleet-share-walk-{source}.jsonl"
    if walk_log.exists():
        shutil.copy(walk_log, output / f"{seed_file.stem}.walk-{source}.jsonl")
    return status == 0, summarize_walk(output / f"{seed_file.stem}.walk-{source}.jsonl")


def summarize_walk(records_file):
    if not records_file.exists():
        return "no records"
    records = [json.loads(line) for line in records_file.read_text(encoding="utf-8").splitlines() if line]
    shared = sum(record["shared"] for record in records)
    failed = [f"{record['item']}: {record['problem']}" for record in records if not record["passed"]]
    unconfirmed = [record["twin"] for record in records if record.get("ownershipUnconfirmed")]
    side_effects = [record["twin"] for record in records if record.get("ownedWithoutDelivery")]
    lines = [f"{len(records)} items ({shared} shared), {len(failed)} failed"]
    lines += [f"      FAIL {failure}" for failure in failed]
    if unconfirmed:
        lines.append(f"      received, owning not readable: {', '.join(unconfirmed)}")
    if side_effects:
        lines.append(f"      unshared but owned through another gift: {', '.join(side_effects)}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--install", type=Path, default=REPOSITORY / "x64" / "Release")
    parser.add_argument("--settings", type=Path, help="launcher settings; default: the last item pool request")
    parser.add_argument("--seed", type=int, default=80302)
    parser.add_argument("--baseline", type=Path, default=REPOSITORY / "combo" / "tests" / "data" /
                        "fleet_mod_pairs.json")
    parser.add_argument("--output", type=Path, help="default: <install>/logs/share-check")
    parser.add_argument("--walk", action="store_true", help="also give the items in the games and watch them")
    parser.add_argument("--reuse-seeds", action="store_true", help="check the seeds a previous run generated")
    args = parser.parse_args()
    install = args.install.resolve()
    settings = args.settings or install / "saves" / "fleet" / "pool" / "settings.json"
    output = (args.output or install / "logs" / "share-check").resolve()
    output.mkdir(parents=True, exist_ok=True)
    baseline = args.baseline.resolve()

    failures = 0
    choices = list_choices(install, baseline, output)
    reference = output / "none.json"
    for mode, choices_file in choices.items():
        arguments = share_arguments(install, choices_file, baseline, output)
        seed_file = output / f"{mode}.json"
        reused = args.reuse_seeds and seed_file.exists()
        if not reused and not generate(install, settings, args.seed, arguments, seed_file):
            print(f"{mode}: generation FAILED, see {seed_file.with_suffix('.log')}")
            failures += 1
            continue
        report = check(install, seed_file, reference, baseline, arguments)
        summary = report.get("summary", {})
        print(f"{mode}: {'PASS' if report['passed'] else 'FAIL'} - {summary.get('sharedFamilies')} families, "
              f"{summary.get('modPairs')} mod pairs, {summary.get('copyGroupsChecked')} shared groups and "
              f"{summary.get('unsharedPairsChecked')} unshared pairs counted")
        for failure in report.get("failures", []):
            print(f"    {failure}")
        failures += 0 if report["passed"] else 1
        if args.walk:
            for source in ("oot", "mm"):
                passed, summary = walk(install, seed_file, source, baseline, output)
                print(f"    walk {source}: {'PASS' if passed else 'FAIL'} - {summary}")
                failures += 0 if passed else 1
    print(f"reports in {output}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
