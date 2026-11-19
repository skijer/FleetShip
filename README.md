# Fleet

Play Ocarina of Time and Majora's Mask in one application, separately or as a single cross-game randomizer.

Fleet runs [Ship of Harkinian](https://github.com/skijer/Shipwright) and
[2 Ship 2 Harkinian](https://github.com/skijer/2ship2harkinian). You need your own ROMs; none are included.

## Play

1. Download the zip for your system from [Releases](../../releases) and unzip it.
2. Open `Ship/soh` and select your Ocarina of Time ROM. When asked, select your Majora's Mask ROM too.
3. Open `2Ship/2ship` and select your Majora's Mask ROM. When asked, select your Ocarina of Time ROM too.
4. Open each game once more and allow it to save its shared assets.
5. Open `fleet` and choose **Combo (OoT + MM)**.

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
