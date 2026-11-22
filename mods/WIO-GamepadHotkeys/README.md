# WattzIO - Gamepad MCM Hotkey Bridge - F4SE

Bind any MCM Hotkey to a Gamepad button.

This mod bridges any mod's MCM hotkeys with gamepad inputs through simulated keys.

## Features

| What you do | What happens |
|---|---|
| Press your chosen gamepad button | The other mod's MCM hotkey fires, and the button's normal action is blocked (optional) |
| Press the original keyboard hotkey | Still works, exactly as before |

Ten slots, so ten different mods' hotkeys can live on your controller at once.

## Initial Setup

Open the Mod Configuration Menu, find **WattzIO - Gamepad MCM Hotkeys**, and set up one slot at a
time:

1. Pick a gamepad button. Optionally pick a second gamepad button to act as a modifier / shift key (if set, the slot only triggers when both are held).
2. Pick a **simulated key** - Numpad and Navigation Keys are typically unused, and F1-F12, 0-9, and A-Z
   are available too.
3. Go into the *other* mod's MCM page and bind the desired hotkey to the key chosen in step 2.

Pressing the gamepad button should now fire that mod's hotkey.

## Options

- **Ten hotkey slots**, each with a gamepad button, an optional gamepad modifier, and a simulated
  key.
- **Discovered Hotkeys** is a second page listing every MCM hotkey found across your installed mods
  and what key each is currently bound to, so you can pick a simulated key that isn't already
  claimed by something else.
- **Disable Hotkeys During Menus** on the Advanced page decides whether hotkeys fire while a menu
  or a prompt is on screen, and each slot can override it.
- **Block Buttons' Normal Actions** on the Advanced page stops a bound button doing what it normally
  does while its hotkey fires (the main button only, or the modifier too), and each slot can
  override it.

## Technical Stuff and Limitations

- There is no duplicate detection, so two hotkeys bound to the same simulated key will both fire.
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

Licensed MIT. [Source](https://github.com/AbbieDoobie/WattzIO/tree/main/mods/WIO-GamepadHotkeys)

## Changelog

### 1.1.0
- Added F1-F12, 0-9, and A-Z to the Simulated Key options.
- Added Block Button's Normal Action, so a bound gamepad button no longer does its usual thing while
  its hotkey fires. Set per slot, with a global default on the Advanced page (Main Button Only).
- Block Hotkeys During Menus is now Disable Hotkeys During Menus, with clearer option names and a
  note under each option explaining it. Existing settings carry over.
### 1.0.0
- Initial release.

## Building

Requires [xmake](https://xmake.io) 3.0.0 or newer and a C++23 compiler (MSVC or Clang-CL).

This mod lives in a monorepo alongside the rest of the WattzIO Fallout 4 mods, and is
built from its own directory rather than the repository root:

```bat
git clone <url-of-this-repo> wattzio
cd wattzio
git submodule update --init mods/WIO-GamepadHotkeys/lib/commonlibf4rd
cd mods\WIO-GamepadHotkeys
xmake f -m releasedbg
xmake
```

Every mod pins its own copy of the dependency, so cloning with `--recurse-submodules`
fetches one for all of them. Initialising just this mod's submodule is enough to build it.

The built plugin lands at `build\windows\x64\releasedbg\WIO-GamepadHotkeys.dll`.

| Path | Contents |
|---|---|
| `src/` | Plugin source: header-only modules plus `main.cpp` |
| `data/` | MCM config and translation files, mirroring the game's `Data` folder |
| `lib/commonlibf4rd` | CommonLibF4RD (pinned submodule). |
| `xmake.lua` | Build configuration |

Every address the plugin resolves is marked with an `F4RD:<kind>` tag and listed in an
`F4RD RELOCATIONS` banner in the file that resolves it, so `grep -rn "F4RD RELOCATIONS" src`
enumerates them. Each banner names the kind, the site, and the id or offset resolved.
