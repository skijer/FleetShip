# Fleet: Ocarina of Time and Majora's Mask in one randomizer

Play Ocarina of Time and Majora's Mask separately, as one combined randomizer, or as part of an Archipelago multiworld.

**[Watch the trailer](https://youtu.be/1HjUWtOB-K0)**

You need your own ROMs. None are included.

| ROM | Versions Fleet reads |
| --- | --- |
| Ocarina of Time | N64 NTSC 1.0, 1.1 or 1.2 |
| Majora's Mask | N64 US or GameCube US |

Other regions and re-releases, such as the LodgeNet or Virtual Console dumps, do not work.

## Set up

1. Download the zip for your system below: **Fleet-windows.zip**, **Fleet-linux.zip** or **Fleet-mac.zip**.
2. Unzip it into a normal folder, such as `C:\Games\Fleet`. Do not run it from inside the zip, a temporary folder or
   OneDrive.
3. Open `fleet` (`fleet.exe` on Windows).
4. Fleet asks for your Ocarina of Time ROM, then your Majora's Mask ROM. Fleet then opens each game by itself, builds
   its files and closes it again. This takes a few minutes and happens only once.
5. Choose **Combo (OoT + MM)**, **Ocarina of Time** or **Majora's Mask**.

Windows also needs the
[Microsoft Visual C++ 2015-2022 x64 runtime](https://aka.ms/vs/17/release/vc_redist.x64.exe).

Keep the folder together: Fleet runs the games from `Ship` and `2Ship`, loads its mods from `mods` and keeps saves in
`saves`.

### Updating

On Windows, the game chooser offers updates. Fleet and both games update together, because they only work at matching
versions. On Linux and macOS, download the new zip and unzip it over the old folder.

Fleet remembers where your ROMs are. If an update needs the game files rebuilt, it does that without asking for the
ROMs again.

### Steam Deck and Linux

Do the first start in Desktop Mode, where the ROM picker and Fleet's questions are easy to answer.

## Archipelago

Fleet plays in an [Archipelago](https://archipelago.gg) multiworld as the game **Fleet**. One slot can be Ocarina of
Time, Majora's Mask or the combo. Archipelago 0.6.4 or newer is required.

### 1. Export your seed (each Fleet player)

1. In Fleet's game chooser, choose **Archipelago** and type your slot name (16 characters at most).
2. Export the seed you want to play:
   - **Ocarina of Time** or **Majora's Mask** alone: press **Export Ocarina of Time** or **Export Majora's Mask**.
     It uses that game's own randomizer settings.
   - **Combo**: open a combo file, go to its **Randomizer** page and press **Export for Archipelago**.
3. Wait while Fleet measures the logic of every check. This can take a few minutes.

Fleet writes two files into the `archipelago` folder beside it:

- `fleet.apworld`: the world Archipelago needs for Fleet.
- `<your slot name>.yaml`: your player file. Do not edit it; it carries your seed.

### 2. Generate the multiworld (the host)

1. Put `fleet.apworld` in Archipelago's `custom_worlds` folder.
2. Put every player's YAML in Archipelago's `Players` folder.
3. Generate the game as usual, then host it on [archipelago.gg](https://archipelago.gg/uploads) or on your own
   server.

**More than one Fleet player?** Each export adds its own names to the world, so the host needs a `fleet.apworld` that
knows all of them. Every Fleet player sends the host their YAML and their export folder from
`archipelago/exports/`. The host copies those folders into the host's own `Fleet/archipelago/exports/` and exports
any seed once. The new `fleet.apworld` covers every Fleet player.

### 3. Connect and play

1. In Fleet, choose **Archipelago** and type the same slot name.
2. Enter the server address of the room (for example `archipelago.gg:38281`) and the password, if it has one.
3. Press **Connect and Play**. Fleet opens the game mode your slot was exported for.

Play on the computer you exported on. To play on another one, copy your folder from `archipelago/exports/` into the
same place in that computer's Fleet.

## When something goes wrong

- **"Not a ROM Fleet reads"**: the dump is another version. Check the table at the top.
- **"Some game files could not be built"**: Fleet names the game and its `logs` folder. Send that log when you ask for
  help.
- **A game window shows "Not responding" during setup**: it is still building its files. Wait for it to close by
  itself.
- **Archipelago says the slot was exported on another computer**: copy that slot's folder from
  `archipelago/exports/` as described above.
