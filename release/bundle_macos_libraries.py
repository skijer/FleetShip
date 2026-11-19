"""Copy the Homebrew libraries the macOS host links beside it, so players need no Homebrew."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

HOMEBREW_DEPENDENCY = re.compile(r"^\s+(/(?:opt/homebrew|usr/local)/\S+\.dylib)")
# sdl2-compat loads SDL3 with dlopen, so no load command names it.
RUNTIME_LOADED = {"libSDL2-2.0.0.dylib": "libSDL3.0.dylib"}


def run(*command):
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def list_homebrew_dependencies(binary):
    return [match.group(1) for line in run("otool", "-L", str(binary)).splitlines()[1:]
            if (match := HOMEBREW_DEPENDENCY.match(line))]


def copy_library(source, folder):
    copy = folder / source.name
    shutil.copy2(source.resolve(), copy)
    copy.chmod(0o755)
    run("install_name_tool", "-id", f"@rpath/{source.name}", str(copy))
    return copy


def bundle(folder):
    pending = [folder / "fleet", folder / "libultraship.dylib"]
    modified = list(pending)
    copied = set()
    while pending:
        binary = pending.pop()
        for dependency in map(Path, list_homebrew_dependencies(binary)):
            run("install_name_tool", "-change", str(dependency), f"@rpath/{dependency.name}", str(binary))
            if dependency.name in copied:
                continue
            copied.add(dependency.name)
            pending.append(copy_library(dependency, folder))
            modified.append(pending[-1])
            companion = RUNTIME_LOADED.get(dependency.name)
            if companion and companion not in copied:
                copied.add(companion)
                homebrew_lib = Path(run("brew", "--prefix").strip()) / "lib"
                pending.append(copy_library(homebrew_lib / companion, folder))
                modified.append(pending[-1])
    for binary in modified:
        run("codesign", "--force", "--sign", "-", str(binary))
    leftovers = {binary.name: list_homebrew_dependencies(binary) for binary in modified}
    leftovers = {name: paths for name, paths in leftovers.items() if paths}
    if leftovers:
        sys.exit(f"Homebrew paths remain: {leftovers}")
    print("Bundled:", ", ".join(sorted(copied)))


if __name__ == "__main__":
    bundle(Path(sys.argv[1]))
