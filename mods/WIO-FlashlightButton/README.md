# WattzIO - Flashlight Button (no more hold!) - F4SE

One dedicated button to turn your Pip-Boy light on and off. No need to hold the button!

Tap the key to turn the flashlight on and off. Optionally, turn on a hold action which handles
Toggle POV / workshop mode (intended for controller).

## Features

| What you do | What happens |
|---|---|
| Tap | Pip-Boy Flashlight on / off |
| Hold, then release **(if enabled)** | Change view (first / third person) |
| Hold and move stick or mouse wheel **(if enabled)** | Zoom the camera (third person) |
| Hold longer, while in a settlement **(if enabled)** | Opens Workshop mode |

## Initial Setup

Open the Mod Configuration Menu and pick a button under **WattzIO - Flashlight Button**. You can bind
a controller button, a keyboard key or mouse button, or both.

If you want to reuse the vanilla button, set **Unbind Vanilla Toggle POV** for that device to
*Unbind Button* on the same page, then close the pause menu. Otherwise, a tap will both toggle the
flashlight and change your view.

## Options

- **Flashlight Input (Gamepad)** and **Flashlight Hotkey (Keyboard)** set the button.
- **Flashlight Modifier (Gamepad - Optional)** is optional. When set, the modifier button has to be held
  down for the main button to work.
- **Unbind Vanilla Toggle POV (Gamepad / Keyboard)** frees up the vanilla Toggle POV button. Pick
  *Unbind Button* or *Rebind Button*; it applies when you close the pause menu, then resets itself to
  *No Change*. The **Status** line underneath shows what the button is bound to right now.
- **Enable Change View and Workshop** turns the hold portion on. **off by default**
- **Hold Time (Tenths of a Second)** sets how long a hold has to be before it stops counting as a
  tap. Default 4, so 0.4 seconds. Only matters if the setting above is on.
- **Block Hotkeys During Menus** controls whether the button fires while the game has a prompt or
  menu showing. *Block Menus Only* (the default) blocks complex UI (e.g. Quick Loot, dialogue,
  lockpicking, VATS, Workshop, Pip-Boy) but leaves simple crosshair prompts like "A to
  Activate" alone. *Block Prompts and Menus* blocks both. *Don't Block* never blocks.

## Technical Stuff and Limitations

- The 'hold' portion doesn't redo any vanilla logic. When you hold the button long enough, everything is
  managed by the game like it would have been. POV change, camera zoom, and Workshop mode all behave
  like normal.
- F4RD came out late into development, so I haven't tested my mods with it as much as I would have liked.
  It's incredibly cool, but I had 50+ hours of mod play/test time prior to that.

## Requirements

- **Game version**: tested only on AE 1.11.221. Addresses for OG (1.10.163) and NG (1.10.984) are
included and it should work, but neither has been tested.
- [F4SE](https://www.nexusmods.com/fallout4/mods/42147)
- [MCM](https://www.nexusmods.com/fallout4/mods/21497)
- [Runtime Database](https://www.nexusmods.com/fallout4/mods/108394)
- Microsoft Visual C++ Redistributable
- *(Soft Requirement)* [WattzIO - Control Unbinder](https://www.nexusmods.com/fallout4/mods/108756): Only needed for MCM
  options that remove vanilla mappings (to free up buttons).

## Credits

Mod by Abbie Doobie. Credit to jarari (Melee and Throw) for the technique of rewriting a real
input event to drive vanilla behaviour from a custom key.

Vector assets in the images are from uxwing.

## License

Licensed MIT. [Source](https://github.com/AbbieDoobie/WattzIO/tree/main/mods/WIO-FlashlightButton)

## Changelog

**1.0.1**

Fixed an issue causing OG to crash.

**1.0.0**

Initial release.

## Building

Requires [xmake](https://xmake.io) 3.0.0 or newer and a C++23 compiler (MSVC or Clang-CL).

```bat
git clone --recurse-submodules <url-of-this-repo>
xmake f -m releasedbg
xmake
```

Cloned without `--recurse-submodules`? Fetch the dependency first:

```bat
git submodule update --init --recursive
```

The built plugin lands at `build\windows\x64\releasedbg\WIO-FlashlightButton.dll`.

| Path | Contents |
|---|---|
| `src/` | Plugin source: header-only modules plus `main.cpp` |
| `data/` | Authored MCM config and translation files, mirroring the game's `Data` folder |
| `lib/commonlibf4rd` | CommonLibF4RD (pinned submodule). |
| `xmake.lua` | Build configuration |

Setting `WIO_PROJECTS_ROOT` makes the build stage the DLL and `data/` into a deploy
folder automatically. Without it the build still succeeds and the deploy step is
skipped. To install a local build by hand, copy the contents of `data\` into your
`Data` folder and the built DLL into `Data\F4SE\Plugins\`.

Every address the plugin resolves is marked with an `F4RD:<kind>` tag and listed in an
`F4RD RELOCATIONS` banner in the file that resolves it, so `grep -rn "F4RD RELOCATIONS" src`
enumerates them. Each banner names the kind, the site, and the id or offset resolved.
