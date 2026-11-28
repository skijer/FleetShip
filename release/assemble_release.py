"""Assemble a player's Fleet zip from the built program, the published games and Fleet's own mods."""
import argparse
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
GAME_FOLDERS = {"ship.zip": "Ship", "2ship.zip": "2Ship"}
ROM_DERIVED_ARCHIVES = {"oot.o2r", "oot-mq.o2r", "oot-unbound.o2r", "mm.o2r", "mm-unbound.o2r"}


def extract_zip(archive, target):
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            extracted = Path(source.extract(entry, target))
            mode = entry.external_attr >> 16
            if mode:
                extracted.chmod(mode)


def is_elf_executable(path):
    with path.open("rb") as header:
        return bool(path.stat().st_mode & 0o111) and header.read(4) == b"\x7fELF"


def unpack_appimage(folder):
    for appimage in folder.glob("*.appimage"):
        appimage.chmod(0o755)
        subprocess.run([str(appimage.resolve()), "--appimage-extract"], cwd=folder, check=True)
        usr = folder / "squashfs-root" / "usr"
        launchers = [folder / path.name for path in (usr / "bin").iterdir() if path.is_file() and is_elf_executable(path)]
        # Fleet loads the game module from the game folder, so usr/bin and usr/lib are flattened into it.
        shutil.copytree(usr / "bin", folder, dirs_exist_ok=True)
        shutil.copytree(usr / "lib", folder, dirs_exist_ok=True)
        shutil.rmtree(folder / "squashfs-root")
        appimage.unlink()
        # The launcher still extracts the ROM; its install rpath pointed at the AppImage's ../lib.
        for launcher in launchers:
            subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(launcher)], check=True)


def unpack_disk_image(folder):
    for image in folder.glob("*.dmg"):
        mount = folder / "mounted"
        subprocess.run(["hdiutil", "attach", str(image), "-nobrowse", "-mountpoint", str(mount)], check=True)
        for bundle in mount.glob("*.app"):
            for contents in ("MacOS", "Resources"):
                shutil.copytree(bundle / "Contents" / contents, folder, dirs_exist_ok=True)
        subprocess.run(["hdiutil", "detach", str(mount)], check=True)
        image.unlink()


def stage_game(archive, target, platform):
    extract_zip(archive, target)
    if platform == "linux":
        unpack_appimage(target)
    elif platform == "mac":
        unpack_disk_image(target)


def refuse_rom_data(staging):
    leaked = [path for path in staging.rglob("*.o2r") if path.name.lower() in ROM_DERIVED_ARCHIVES]
    if leaked:
        sys.exit(f"ROM-derived archives must never be packaged: {leaked}")


def write_zip(staging, archive_path):
    with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(staging.rglob("*")):
            if path.is_file():
                entry = zipfile.ZipInfo.from_file(path, Path(staging.name) / path.relative_to(staging))
                entry.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(entry, path.read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=["windows", "linux", "mac"], required=True)
    parser.add_argument("--edition", choices=["release", "unsafe"], required=True)
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--games", type=Path, required=True)
    parser.add_argument("--mods", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    version = (REPOSITORY_ROOT / "VERSION").read_text(encoding="utf-8").strip()
    suffix = "-unsafe" if args.edition == "unsafe" else ""
    staging = args.output / f"Fleet-{version}-{args.platform}{suffix}"
    shutil.copytree(args.program, staging)
    # Workflow artifacts drop the executable bit.
    host = staging / "fleet"
    if host.exists():
        host.chmod(0o755)
    shutil.copytree(args.mods, staging / "mods")
    shutil.copy2(REPOSITORY_ROOT / "release" / "README-players.txt", staging / "README.txt")
    shutil.copy2(REPOSITORY_ROOT / "release" / "GUIDE.md", staging / "GUIDE.md")
    # Fleet's updater reads this to know its version and to update only the edition it was installed as.
    (staging / "fleet-install.json").write_text(json.dumps({"version": version, "edition": args.edition}) + "\n",
                                                encoding="utf-8")
    for archive_name, folder in GAME_FOLDERS.items():
        stage_game(args.games / archive_name, staging / folder, args.platform)
    shutil.copy2(staging / "Ship" / "gamecontrollerdb.txt", staging / "gamecontrollerdb.txt")
    refuse_rom_data(staging)
    write_zip(staging, args.output / f"Fleet-{args.platform}{suffix}.zip")


if __name__ == "__main__":
    main()
