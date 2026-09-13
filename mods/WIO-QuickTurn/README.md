# WattzIO - Quick Turn Button - F4SE

Quick 180, or omnidirectional (for RE2R fans)! Options for input and turn speed are included.

## Features

| Movement Modifier | What the button does |
|---|---|
| **Off** | Turn 180, regardless of movement direction (LS / WSAD) |
| **Backwards Only** | Turn 180, but only while holding movement backwards  |
| **Cardinals Only** | Turn to the nearest 90, relative to the direction held |
| **Omnidirectional** | Turn to the exact direction held |

| Input | How it works |
|---|---|
| **Shared Action** | Quick turn will trigger only when movement is held in an included direction. Otherwise, the vanilla action happens (e.g. Sneaking). Intended for Gamepad. |
| **Direct Input** | Dedicated quick turn hotkey. Intended for Keyboard. |

## Initial Setup

Open the Mod Configuration Menu, find **WattzIO - Quick Turn**, and set a Shared Action Input or Hotkey
(or both). Adjust Quick Turn Direction to match the kind of quick turn you are looking for.

## Options

- **Shared Action for Quick Turn** picks the vanilla action whose button also Quick Turns: Sneak,
  Sprint, Activate or Ready/Reload. **Default Off.**
- **Quick Turn Direction (Shared Action)** picks which held direction makes that button turn
  instead of doing its vanilla action: Backwards Only, Cardinals Only or Omnidirectional.
- **Quick Turn Input (Gamepad)** and **Quick Turn Hotkey (Keyboard)** set a dedicated Direct Input
  button. The keyboard hotkey can include Shift, Ctrl or Alt.
- **Quick Turn Modifier (Gamepad - Optional)**, if set, makes the gamepad Direct Input button fire
  only while this button is also held. Shared Action is unaffected. **Default None.**
- **Quick Turn Direction (Direct Input)** will only quick turn if the current movement direction
  matches what is set. **Default Off** (always 180).
- **Apply in First Person** turns Quick Turn on or off in first person. **Default On.**
- **Apply in Third Person** picks what a third-person Quick Turn turns: **Turn Camera** (default)
  swings the view around, **Turn Player** turns only your character, **Turn Both** turns both
  together, or **Off**. With a weapon drawn the camera always follows your character, so every
  option except Off turns both.
- **Turn Duration** is how long the turn takes. 0-500ms, **default 100ms**, with 0 being an instant
  snap.
- **Block Hotkeys During Menus** stops Quick Turn firing while a menu (Block Menus Only) or also a
  crosshair prompt (Block Prompts and Menus) is up. **Default Don't Block.**

## Technical Stuff and Limitations

- Movement direction is based on input, not your character's current movement direction in world.
- In third person with your weapon holstered, movement is relative to the camera. A Turn Camera
  Quick Turn while still holding a direction will have your character turn to follow it.
- F4RD came out late into development, so I haven't tested my mods with it as much as I would have
  liked. It's incredibly cool, but I had 50+ hours of mod play/test time prior to that.

## Requirements

- **Game version**: tested on AE (1.11.221) and OG (1.10.163). Addresses for NG (1.10.984) are
  included and it should work, but has not been tested.
- [F4SE](https://www.nexusmods.com/fallout4/mods/42147)
- [MCM](https://www.nexusmods.com/fallout4/mods/21497)
- [Runtime Database](https://www.nexusmods.com/fallout4/mods/108394)
- Microsoft Visual C++ Redistributable

## Credits

Mod by Abbie Doobie.

Vector assets in the images are from uxwing.

## License

Licensed MIT. [Source](https://github.com/AbbieDoobie/WattzIO/tree/main/mods/WIO-QuickTurn)

## Changelog

Initial release.

## Building

Requires [xmake](https://xmake.io) 3.0.0 or newer and a C++23 compiler (MSVC or Clang-CL).

This mod lives in a monorepo alongside the rest of the WattzIO Fallout 4 mods, and is
built from its own directory rather than the repository root:

```bat
git clone <url-of-this-repo> wattzio
cd wattzio
git submodule update --init mods/WIO-QuickTurn/lib/commonlibf4rd
cd mods\WIO-QuickTurn
xmake f -m releasedbg
xmake
```

Every mod pins its own copy of the dependency, so cloning with `--recurse-submodules`
fetches one for all of them. Initialising just this mod's submodule is enough to build it.

The built plugin lands at `build\windows\x64\releasedbg\WIO-QuickTurn.dll`.

| Path | Contents |
|---|---|
| `src/` | Plugin source: header-only modules plus `main.cpp` |
| `data/` | MCM config and translation files, mirroring the game's `Data` folder |
| `lib/commonlibf4rd` | CommonLibF4RD (pinned submodule). |
| `xmake.lua` | Build configuration |

Every address the plugin resolves is marked with an `F4RD:<kind>` tag and listed in an
`F4RD RELOCATIONS` banner in the file that resolves it, so `grep -rn "F4RD RELOCATIONS" src`
enumerates them. Each banner names the kind, the site, and the id or offset resolved.
