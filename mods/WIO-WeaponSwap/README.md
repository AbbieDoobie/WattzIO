# WattzIO - Weapon Swap Button - F4SE

One button to swap between your favorite weapons. Intended for a 2 ~ 4 weapon loadout, but every
favorite can be cycled through if you want.

## Features

| What you do | What happens |
|---|---|
| Press | Swap weapons, according to Weapon Swap Type (either between 1 and 2, or cycle between all favorites) |
| Hold, then release **(if enabled)** | Equip favorite 3 |
| Triple tap **(if enabled)** | Equip favorite 4 |

## Initial Setup

Open the Mod Configuration Menu and pick a Gamepad or Keyboard key in **WattzIO - Weapon Swap**.
Favorite the weapons you want to swap between, just like you would normally.

## Options

- **Weapon Swap Type** is *Slots 1 and 2* or *All Slots*.
- **Only Equip Weapons** skips any favorite that isn't a weapon.
- **Hold (Slot 3)**: hold the button, then release, to equip favorite 3. **Off by
  default.**
- **Triple Tap (Slot 4)**: tap the button three times in quick succession to equip favorite
  4. **Off by default.**
- **Hold Time** sets how long a hold has to be.
- **Triple Tap Wait Time** sets the time window after the first tap that all 3 must fall within.
- **If Weapon is Holstered or No Weapon is Equipped** decides what a press does in that state: equip
  and draw, equip without drawing, do nothing, or just draw whatever's already equipped without
  swapping.

## Technical Stuff and Limitations

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
git submodule update --init mods/WIO-WeaponSwap/lib/commonlibf4rd
cd mods\WIO-WeaponSwap
xmake f -m releasedbg
xmake
```

Every mod pins its own copy of the dependency, so cloning with `--recurse-submodules`
fetches one for all of them. Initialising just this mod's submodule is enough to build it.

The built plugin lands at `build\windows\x64\releasedbg\WIO-WeaponSwap.dll`.

| Path | Contents |
|---|---|
| `src/` | Plugin source: header-only modules plus `main.cpp` |
| `data/` | MCM config and translation files, mirroring the game's `Data` folder |
| `lib/commonlibf4rd` | CommonLibF4RD (pinned submodule). |
| `xmake.lua` | Build configuration |

Every address the plugin resolves is marked with an `F4RD:<kind>` tag and listed in an
`F4RD RELOCATIONS` banner in the file that resolves it, so `grep -rn "F4RD RELOCATIONS" src`
enumerates them. Each banner names the kind, the site, and the id or offset resolved.
