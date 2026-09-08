# WattzIO - Activate Reload Combo Button - F4SE

One button that activates, reloads, and holsters. Made to save a button on controller (where it
matters most), but the options are there for keyboard and mouse too.

The Activate button keeps working just like it always did. Ready/Reload/Holster just triggers
alongside, with reload on tap and holster on hold.

## Features

| What you do | What happens |
|---|---|
| Tap | Activate / interact, just like normal |
| Tap | Reload (occurs on release) |
| Hold, then release | Ready/Holster weapon |
| Tap Secondary Action **(if set)** | UI prompt 2 (namely Quick Loot's transfer) |
| Hold Secondary Action **(if enabled)** | When in Power Armor, climb out of it |

Enable Combo is **on by default for gamepad** and **off by default for keyboard and mouse**. The
options are independent, since the whole point is cutting down the button count on controller.

## Initial Setup

By default the combo is enabled on Gamepad, but if you want to free up a key (unbind Ready/Reload)
then there are a few more steps.

Open the Mod Configuration Menu, find **WattzIO - Activate Combo**, and set **Secondary Action** to a
button (I recommend whatever you have sprint set to, by default LS/L3). That key is for UI prompt 2
(e.g. Quick Loot's Transfer) to its own button, which matters because it normally shares your
Ready/Reload key. If you want the vanilla Ready/Reload button freed up for something else, use
**Unbind Vanilla Ready/Reload Key** on that device.

## Options

- **Enable Combo (Gamepad)** and **Enable Combo (Keyboard)** turn the combo on per device. Gamepad
  is **on by default**, keyboard is **off by default**.
- **Secondary Action (Gamepad)** and **Secondary Action (Keyboard)** give prompt 2 its
  own binding. Unset by default. The on-screen prompt will update to match. **I highly recommend
  the sprint button (LS/L3 by default)**.
- **Unbind Vanilla Ready/Reload Key (Gamepad / Keyboard)** frees up the vanilla Ready/Reload button.
- **Exit Power Armor With Secondary Action** moves climbing out of Power Armor to your Secondary
  Action button, so holding Activate to holster doesn't also make you climb out. **Off by default.**
  While you are in Power Armor it also moves hold-to-pick-up onto the same button.
- **Silence Empty Activate Sound** stops the quiet vanilla "nothing to activate" blip. **On by
  default**, since Activate now fires alongside every reload and holster, and that sound would
  otherwise play on every press and get really annoying.
- **Unholster Hold Time** sets how long a hold has to be to trigger unholstering/drawing your
  weapon. Note that holstering uses vanilla timing.
- **Block Companion Orders While Weapon Drawn** stops Activate from starting command mode on
  companions when weapon is drawn. Other interactions are unaffected. **Off by default.**
- **Block NPC Interaction In Combat** stops Activate from starting command mode on companions
  and/or talking to NPCs while in combat. Other interactions are unaffected. **Off by default.**
- **Extend Block After Combat Ends** keeps the block above active for a few seconds after combat
  ends. **1 second by default.**

## Technical Stuff and Limitations

- "Special items" with a second prompt (namely holotapes) are not covered *yet* and still follow
  whatever Ready/Reload is bound to. When that's unbound, I've found that I can "grab" the holotape
  physically (hold activate), and on release it plays. Not sure why, but that works for me
  consistently. Alternatively you can play them from inventory.
- Activate/Interact is untouched, so any unique prompts should just work.
- **menu** controls are untouched. I've had no issue with any menus, whether vanilla, FallUI, or
  one-offs from other mods.
- Reload and holster also fire an Activate. It hasn't been a problem for me, but its worth
  mentioning.
- F4RD came out late into development, so I haven't tested my mods with it as much as I would have
  liked. It's incredibly cool, but I had 50+ hours of mod play/test time prior to that.

## Requirements

- **Game version**: tested on AE (1.11.221) and OG (1.10.163). Addresses for NG (1.10.984) are
  included and it should work, but has not been tested.
- [F4SE](https://www.nexusmods.com/fallout4/mods/42147)
- [MCM](https://www.nexusmods.com/fallout4/mods/21497)
- [Runtime Database](https://www.nexusmods.com/fallout4/mods/108394)
- Microsoft Visual C++ Redistributable
- *(Soft Requirement)* [WattzIO - Control Unbinder](https://www.nexusmods.com/fallout4/mods/108756):
  Only needed for MCM options that remove vanilla mappings (to free up buttons).

## Credits

Mod by Abbie Doobie.

Vector assets in the images are from uxwing.

## License

Licensed MIT. [Source](https://github.com/AbbieDoobie/WattzIO/tree/main/mods/WIO-ActivateCombo)

## Changelog

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

The built plugin lands at `build\windows\x64\releasedbg\WIO-ActivateCombo.dll`.

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
