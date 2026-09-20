# WattzIO - Gamepad Binds for Keyboard Keys - F4SE

Bind keyboard-only controls to your gamepad, including **Quick Save**, **Quick Load**,
**Auto-Move**, Pip-Boy pages, and more!

These are not hotkeys. This mod is an in-between layer of sorts, which sends the vanilla action
from a gamepad button press.

## Features

| What you do | What happens |
|---|---|
| Press a set button | The event triggers, just like if a keyboard key had been pressed |
| Press a set button with its modifier also held | ditto, but had the main button been pressed alone it would have triggered its normal gamepad action |
| Press the keyboard key | No change, works like it always did |
| Press a set button while in a menu (e.g. Quick Loot is open) | No special event is triggered, the normal gamepad action for that button happens |

## Initial Setup

Open the Mod Configuration Menu and find **WattzIO - Gamepad KBM Keys**. Every control starts
unbound, so pick the ones you want on **General Settings**.

All gamepad buttons already do something. By default, a button bound in this mod will stop doing
its normal job. Either pick buttons you can spare, or bind a modifier as well, to keep the button's
normal use available. Think of a modifier like a shift key of sorts, or an input layer in Steam
Input.

## Options

**General Settings**, for each control:

| Setting | What it does |
|---|---|
| *Control name* | The gamepad button that fires it. Default: None. |
| *Control name* Modifier | An optional button that, if set, must also be held to fire the event. Default: None. |
| Block Button's Normal Action | Whether the button's usual action is suppressed. Default: Use Global Setting. |

Blocking choices are **Off (Don't Block)**, **Main Button Only**, and **Main and Modifier**. Off
means you get both the bind and the button's normal action. Main Button Only means the button won't
send its normal gamepad event, but if you happen to set a modifier, the modifier still will. Main
and Modifier prevents both buttons from performing their normal action.

**Advanced** holds the global blocking setting every control uses unless you change it on the
control itself. Default: **Main Button Only**.

### Controls you can bind

| Section | Controls |
|---|---|
| Saving | Quick Save, Quick Load |
| Pip-Boy and Menus | Favorites, Stats, Inventory, Data, Map, Radio |
| Movement | Auto-Move, Run, Always Run, Forward, Back, Strafe Left, Strafe Right |

## Technical Stuff and Limitations

- A bind only works during normal gameplay, the same as the keyboard key. So while quick loot,
  V.A.T.S., dialogue, the Workshop, most menus, or a scope is open, their normal UI controls are
  active instead.
- Nothing is saved to the control map, everything is in memory, and it's safe to uninstall at any
  time.

## Requirements

- **Game version**: tested on AE (1.11.221) and OG (1.10.163). Addresses for NG (1.10.984) are
  included and it should work, but has not been tested.
- [F4SE](https://www.nexusmods.com/fallout4/mods/42147)
- [MCM](https://www.nexusmods.com/fallout4/mods/21497)
- [Runtime Database](https://www.nexusmods.com/fallout4/mods/108394)
- Microsoft Visual C++ Redistributable

## Credits

Mod by Abbie Doobie.

## License

Licensed MIT. [Source](SOURCE_LINK)

## Changelog

Initial release.

## Building

Requires [xmake](https://xmake.io) 3.0.0 or newer and a C++23 compiler (MSVC or Clang-CL).

This mod lives in a monorepo alongside the rest of the WattzIO Fallout 4 mods, and is
built from its own directory rather than the repository root:

```bat
git clone <url-of-this-repo> wattzio
cd wattzio
git submodule update --init mods/WIO-GamepadKbmKeys/lib/commonlibf4rd
cd mods\WIO-GamepadKbmKeys
xmake f -m releasedbg
xmake
```

Every mod pins its own copy of the dependency, so cloning with `--recurse-submodules`
fetches one for all of them. Initialising just this mod's submodule is enough to build it.

The built plugin lands at `build\windows\x64\releasedbg\WIO-GamepadKbmKeys.dll`.

| Path | Contents |
|---|---|
| `src/` | Plugin source: header-only modules plus `main.cpp` |
| `data/` | MCM config and translation files, mirroring the game's `Data` folder |
| `lib/commonlibf4rd` | CommonLibF4RD (pinned submodule). |
| `xmake.lua` | Build configuration |

Every address the plugin resolves is marked with an `F4RD:<kind>` tag and listed in an
`F4RD RELOCATIONS` banner in the file that resolves it, so `grep -rn "F4RD RELOCATIONS" src`
enumerates them. Each banner names the kind, the site, and the id or offset resolved.
