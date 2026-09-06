# WattzIO - Holstered Zoom (3 / New Vegas) - F4SE

Hold the Aim/Block button while your weapon is holstered, or while you have nothing equipped, to
zoom the camera in. Works just like Fallout 3 and New Vegas, no hotkey to bind.

## Features

| What you do | What happens |
|---|---|
| Hold Aim/Block with your weapon holstered | Camera eases in like binoculars |
| Hold Aim/Block with nothing equipped | ditto |
| Release | Camera zooms back out |
| Hold Aim/Block with a weapon drawn | No change from vanilla, you aim or block like normal |

## Initial Setup

Nothing to bind! Though you can change the zoom amount and more options in the Mod Configuration Menu, under **WattzIO - Holstered Zoom**.

## Options

| Setting | Default | Description |
|---|---|---|
| Enable Holstered Zoom | On | Master on/off switch. |
| Zoom Strength (%) | 75% | FOV at full zoom as a percentage of your current FOV. Lower means zoom in further. |
| Zoom Speed (ms) | 100 ms | How long it takes to fully zoom in or out. |
| Apply in First Person | On | Whether the zoom applies in first-person view. |
| Apply in Third Person | On | Whether the zoom applies in third-person view. |
| Analog Trigger Scaling | Off | Gamepad only. Zoom is based on where the trigger is at. |
| Reveal HUD While Zoomed | Off | Requires iHUD. While zoomed, *Compass* reveals just the compass, or *All* reveals the whole HUD. |

### iHUD integration

If you use [Immersive HUD - iHUD](https://www.nexusmods.com/fallout4/mods/20830), setting **Reveal
HUD While Zoomed** to *Compass* or *All* brings that part of the HUD up while you hold zoom.

- How long the elements remain after you release is governed by iHUD's **Fade Time (s)** setting, not by
  this mod.
- The iHUD compass and HUD toggles are not touched, and should keep working independently.
- Same goes for combat, sprinting, weapon-drawn, and every other iHUD trigger. They should all
  behave just like they normally do.

iHUD is not a requirement. Without it, the setting simply does nothing.

## Technical Stuff and Limitations

- The zoom should properly kick out if a real menu opens (Pip-Boy, V.A.T.S., dialogue, or any other
  non-gameplay state). If you find one I missed, please report it. Either way, pressing aim/block (zoom)
  again will fix it.
- Compatible with FOV mods. I had no issue when testing a few options for changing FOV.
- F4RD came out late into development, so I haven't tested my mods with it as much as I would have
  liked. It's incredibly cool, but I had 50+ hours of mod play/test time prior to that.

## Requirements

- **Game version**: tested only on AE 1.11.221. Addresses for OG (1.10.163) and NG (1.10.984) are
included and it should work, but neither has been tested.
- [F4SE](https://www.nexusmods.com/fallout4/mods/42147)
- [MCM](https://www.nexusmods.com/fallout4/mods/21497)
- [Runtime Database](https://www.nexusmods.com/fallout4/mods/108394)
- Microsoft Visual C++ Redistributable
- *(Optional)* [Immersive HUD - iHUD](https://www.nexusmods.com/fallout4/mods/20830): Only needed
  for the Reveal HUD While Zoomed option.

## Credits

Mod by Abbie Doobie.

Vector assets in the images are from uxwing.

## License

Licensed MIT. [Source](https://github.com/AbbieDoobie/WattzIO/tree/main/mods/WIO-HolsteredZoom)

## Changelog

**1.0.1** - Fixed an issue causing the game to crash on Fallout 4 1.10.163 (OG).

**1.0.0** - Initial release.

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

The built plugin lands at `build\windows\x64\releasedbg\WIO-HolsteredZoom.dll`.

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
