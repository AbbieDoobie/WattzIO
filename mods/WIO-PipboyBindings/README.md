# WattzIO - Pip-Boy Hardcoded Bindings Fix - F4SE

Rebind some hardcoded(ish) Pip-Boy buttons: **Close**, **Zoom**, and the button that backs out of
**companion order mode**. Intended for Gamepad.

By default, Pip-Boy close is bound to a universal cancel input that is shared by some stuff (B), not
the Open Pip-Boy binding. So if you rebind Open Pip-Boy, too bad for you, now the keys for open and
close don't match! Unlike on Keyboard, where they do. This really really annoyed me, and is the
only reason I made this mod.

Pip-Boy Zoom is just straight up hardcoded to Select/View on a controller with no way to change it.
Companion order mode is linked to the Open Pip-Boy bind, which on one hand makes sense, but on the
other it is inconsistent since it doesn't use the universal cancel input. Why make this part
un-universal out of nowhere?

## Features

| What you do | What happens |
|---|---|
| Press the mod's Pip-Boy Zoom button | Same as vanilla, just rebindable |
| Press the mod's Pip-Boy Close button | ditto |
| Press the mod's Exit button in companion order mode | ditto |
| Press the vanilla buttons for those actions | They still work like normal, unless you disable them (see Options) |

Changes are made in memory, and only while the relevant mode is actually up. Nothing is saved to the
control map.

This mod is really only intended for Gamepad, but I left options for Keyboard and Mouse too.

## Initial Setup

Open the Mod Configuration Menu and find **WattzIO - Pip-Boy Bind Fix**. Set the buttons you want to
change here. Suggestions are included, based on using Select/View to Open/Close the Pip-Boy (my
personal preference).

## Options

| Setting | What it does |
|---|---|
| Pip-Boy Zoom Input (Gamepad) | Extra gamepad button for zoom. Default: OFF. |
| Pip-Boy Zoom Hotkey (Keyboard) | ditto, but for keyboard. Default: none. |
| Disable Vanilla Zoom Buttons | Stops Select/View, V, and right-click from zooming while the Pip-Boy is open. The mod's Zoom buttons still work. Default: OFF. |
| Pip-Boy Close Input (Gamepad) | Extra gamepad button that closes the Pip-Boy. Default: Same as Pip-Boy Open. |
| Pip-Boy Close Hotkey (Keyboard) | ditto, but for keyboard. Default: none. |
| Exit Order Mode Input (Gamepad) | Extra gamepad button that leaves companion order mode. Default: OFF. |
| Exit Order Mode Hotkey (Keyboard) | ditto, but for keyboard. Default: none. |
| Disable Vanilla Exit Order Mode Button | Stops the Open Pip-Boy button from leaving order mode. Default: OFF. |

Setting Close to **Same as Pip-Boy Open** means the same button opens and closes it, and that
follows along automatically if you later rebind Open Pip-Boy in the game's normal Controls menu.

### Picking a good order mode exit button

**Anything should work**, as the exit button 'owns' whichever button you choose before any other
vanilla actions would occur. So for example, B (the universal-yet-not-universal-because-reasons
button) works here, even if you assign it to Sneak (my personal preference). You won't begin sneaking
when you hit B to exit companion order mode (though you will the next time you press B, of course).

### Picking a good zoom button

Most controller buttons already do something inside the Pip-Boy. A selects, B closes, bumpers change
tabs, D-pad navigates, etc. If you pick one of those, it will do **both** its normal bind and zoom.

**LS/L3, clicking the left stick, is the recommended choice**, as it is nearly unused in the Pip-Boy.
I believe LS/L3 is only used by FallUI Map for one optional function.

Just don't set the same button for both Pip-Boy Zoom and Pip-Boy Close. It can cause issues (plus it
makes Zoom unreachable).

## Technical Stuff and Limitations

- Zoom = Pip-Boy's 'closeness' to the screen. So other zoom-ish things like the mouse wheel remain
  untouched.
- On a controller, B always closes the Pip-Boy. I intentionally did not include a "Disable Vanilla
  Pip-Boy Close Button", as it was problematic.
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

Licensed MIT. [Source](https://github.com/AbbieDoobie/WattzIO/tree/main/mods/WIO-PipboyBindings)

## Changelog

Initial release.

## Building

Requires [xmake](https://xmake.io) 3.0.0 or newer and a C++23 compiler (MSVC or Clang-CL).

This mod lives in a monorepo alongside the rest of the WattzIO Fallout 4 mods, and is
built from its own directory rather than the repository root:

```bat
git clone <url-of-this-repo> wattzio
cd wattzio
git submodule update --init mods/WIO-PipboyBindings/lib/commonlibf4rd
cd mods\WIO-PipboyBindings
xmake f -m releasedbg
xmake
```

Every mod pins its own copy of the dependency, so cloning with `--recurse-submodules`
fetches one for all of them. Initialising just this mod's submodule is enough to build it.

The built plugin lands at `build\windows\x64\releasedbg\WIO-PipboyBindings.dll`.

| Path | Contents |
|---|---|
| `src/` | Plugin source: header-only modules plus `main.cpp` |
| `data/` | MCM config and translation files, mirroring the game's `Data` folder |
| `lib/commonlibf4rd` | CommonLibF4RD (pinned submodule). |
| `xmake.lua` | Build configuration |

Every address the plugin resolves is marked with an `F4RD:<kind>` tag and listed in an
`F4RD RELOCATIONS` banner in the file that resolves it, so `grep -rn "F4RD RELOCATIONS" src`
enumerates them. Each banner names the kind, the site, and the id or offset resolved.
