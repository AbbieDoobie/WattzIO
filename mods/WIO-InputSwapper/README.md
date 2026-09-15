# WattzIO - Auto Input Swap (Gamepad/KBM Switch) - F4SE

Use a gamepad and a keyboard and mouse at the same time. No delay when switching, no dropped
presses. Highly configurable.

An automatic input type switcher that is configurable enough to (hopefully) work for everyone.
Glyphs should not flicker, and the mouse should not randomly take over menus while you are actively
using a gamepad.

**Alternative input-switch mods must be removed or disabled, including the patches in Buffout and
Addictol.**

## Features

| What you do | What happens |
|---|---|
| Use both devices at once | Inputs from both are processed like you'd expect |
| Switch devices | Glyphs update to that input type |
| Set Preferred Button Glyphs | The preferred glyphs re-apply once the other (non-preferred) device has received no input for a configurable timeout. No preference by default. |
| Set Look Input Source | Restrict which device actually turns the camera. No restriction by default. |

## Initial Setup

Nothing needs to be configured for this to work, but there are some options for fine-tuning in the
Mod Configuration Menu under **WattzIO - Input Swapper**.

## Options

- **Preferred Button Glyphs** controls which device's icons and cursor are shown by default. **This
  option has zero impact on actual input;** it only affects glyphs and the mouse cursor in menus.
  - *Auto* shows whichever device you used most recently.
  - *Gamepad* shows gamepad by default, switches to keyboard and mouse while those are active, and
    falls back after the configured delay.
  - *KBM* is the reverse of *Gamepad* above.
- **Switch-Back Delay** (0-10s, **default 2**) sets how long Preferred Button Glyphs waits before
  falling back (0 is instant). It has no effect on *Auto*.
- **Look Input Source** (Advanced) restricts which device actually turns the camera. The other
  device keeps working normally for everything else (e.g. menus).
  - *Both* (**default**) lets both devices drive the camera. Some 'fancy math' is applied to the
    mouse vector in this mode to keep the camera from freaking out, similar to AutoInputSwitch.
  - *Mouse Only* blocks camera movement from the analog stick. When set, no additional math is
    applied to the mouse vector, so if you want to modify the way mouse look works, this is the
    option for you.
  - *Gamepad Only* blocks camera movement from the mouse.
- **Stop Auto-Move with Analog Stick Up/Down** (Advanced, **default off**) cancels Auto-Move when
  you push the movement stick up or down, the same way up/down (W and S) already do on keyboard.

## Technical Stuff and Limitations

- Some prompts do not like the input method switching while they are open, and fixing them is
  complicated (e.g. XDI, or the fast travel accept prompt on the Pip-Boy Map). They work with this
  mod, just as they do with all the other input-switch mods, but if one of those prompts is open and
  you go gamepad -> KBM -> gamepad, you can't accept with the gamepad until you back out. This is a
  super edge case, but it's worth mentioning.
- F4RD came out late into development, so I haven't tested my mods with it as much as I would have
  liked. It's incredibly cool, but I had 50+ hours of mod play/test time prior to that.
- Found a bug? Turn on **Verbose Diagnostics** in the MCM, reproduce it, and send me
  `%userprofile%\Documents\My Games\Fallout4\F4SE\WIO-InputSwapper.log`.

## Requirements

- **Game version**: tested on AE (1.11.221) and OG (1.10.163). Addresses for NG (1.10.984) are
  included and it should work, but has not been tested.
- [F4SE](https://www.nexusmods.com/fallout4/mods/42147)
- [MCM](https://www.nexusmods.com/fallout4/mods/21497)
- [Runtime Database](https://www.nexusmods.com/fallout4/mods/108394)
- Microsoft Visual C++ Redistributable

## Credits

Mod by Abbie Doobie.

**[AutoInputSwitch](https://github.com/Exit-9B/AutoInputSwitch)** by Parapets/Exit-9B is the base I
used when working on this mod. I had experience with it from a Skyrim mod I had been working on for
Wooting keyboards, which ended up being simplified later down the line. All credit to
Parapets/Exit-9B for a lot of this.

**[Addictol](https://github.com/Dear-Modding-FO4/Addictol)** by the Dear-Modding-FO4 team for the
Fallout-specific groundwork. Without it, I wouldn't have known where to start on certain things
like the Pip-Boy.

Vector assets in the images are from uxwing.

## License

Licensed MIT. [Source](https://github.com/AbbieDoobie/WattzIO/tree/main/mods/WIO-InputSwapper)

## Changelog

Initial release.

## Building

Requires [xmake](https://xmake.io) 3.0.0 or newer and a C++23 compiler (MSVC or Clang-CL).

This mod lives in a monorepo alongside the rest of the WattzIO Fallout 4 mods, and is
built from its own directory rather than the repository root:

```bat
git clone <url-of-this-repo> wattzio
cd wattzio
git submodule update --init mods/WIO-InputSwapper/lib/commonlibf4rd
cd mods\WIO-InputSwapper
xmake f -m releasedbg
xmake
```

Every mod pins its own copy of the dependency, so cloning with `--recurse-submodules`
fetches one for all of them. Initialising just this mod's submodule is enough to build it.

The built plugin lands at `build\windows\x64\releasedbg\WIO-InputSwapper.dll`.

| Path | Contents |
|---|---|
| `src/` | Plugin source: header-only modules plus `main.cpp` |
| `data/` | MCM config and translation files, mirroring the game's `Data` folder |
| `lib/commonlibf4rd` | CommonLibF4RD (pinned submodule) |
| `xmake.lua` | Build configuration |

Every address the plugin resolves is marked with an `F4RD:<kind>` tag and listed in an
`F4RD RELOCATIONS` banner in the file that resolves it, so `grep -rn "F4RD RELOCATIONS" src`
enumerates them. Each banner names the kind, the site, and the id or offset resolved.
