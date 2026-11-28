# Fleet

Play Ocarina of Time and Majora's Mask in one application, separately or as a single cross-game randomizer.

Fleet runs [Ship of Harkinian](https://github.com/skijer/Shipwright) and
[2 Ship 2 Harkinian](https://github.com/skijer/2ship2harkinian). You need your own ROMs; none are included.

**[Trailer](https://youtu.be/1HjUWtOB-K0)** · **[Player guide: setup and Archipelago](release/GUIDE.md)**

## Play

1. Download the zip for your system from [Releases](../../releases) and unzip it.
2. Open `fleet` and choose your Ocarina of Time and Majora's Mask ROMs when it asks. Fleet starts each game once
   by itself to build its files, the other game's assets included, then closes it.
3. Choose **Combo (OoT + MM)**.

| ROM | Supported versions |
| --- | --- |
| Ocarina of Time | N64 NTSC 1.0, 1.1, 1.2 |
| Majora's Mask | N64 US, GameCube US |

## Editions

| Edition | Mods it loads |
| --- | --- |
| Release | Signed mods only |
| Unsafe | Any mod, after asking you. For mod developers |

## Build

```sh
git clone --recursive https://github.com/skijer/FleetShip.git
cd FleetShip
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target fleet
ctest --test-dir build -C Release --output-on-failure
```

Format the code with `run-clang-format.sh` (clang-format 14) before opening a pull request.
