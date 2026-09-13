# WattzIO - Control Unbinder - F4SE

Unbind any vanilla control, and bind it back again, from the Mod Configuration Menu. Useful for
freeing up buttons so another mod or hotkey can use them (especially on controller).

Controls are split by input type, with Gamepad and Keyboard getting their own pages.

## Features

| What you do | What happens |
|---|---|
| Set a control to *Unbind Button* | That control's binding is cleared |
| Set a control to *Rebind Button* | That control is rebound |
| Read the status line under a control | Shows what the game currently has bound |

This mod is primarily a service of sorts for the other WattzIO mods.

## Initial Setup

Open the Mod Configuration Menu, find **WattzIO - Control Unbinder**, and open the page for the
device you are using. No controls are changed by default, this is more of a utility.

## Options

- **Gamepad page** - One option per control, including the four Quick Slot D-Pad entries.
- **Keyboard and Mouse page** - One option per control. KBM has more rebindable vanilla buttons, so
  this page has a lot more slots than gamepad.
- Special (normally not editable) bindings can be modified as well, including:
  **Quick Slot Up / Down / Left / Right (D-Pad)**, **Zoom In / Zoom Out (Mouse Wheel)**, and
  **Favorite 1-12 (keys 1-0, Minus, Equals)**.
- **Menu workarounds** (Advanced page, **on by default**) - Keep dialogue, Workshop, Pip-Boy, and
  Favorites menus working when Quick Slots, Zoom, or Favorites are unbound. Leave them on unless
  you have a reason not to.

## Technical Stuff and Limitations

- Keyboard and mouse are one slot as far as the engine is concerned, so unbinding a control on one
  clears it on the other too. Rebinding brings both back to defaults (if both had a key, like Toggle POV).
- If something ever does end up in a state you didn't expect, Settings > Controls > Reset to
  Defaults always restores the proper defaults for everything.
- F4RD came out late into development, so I haven't tested my mods with it as much as I would have
  liked. It's incredibly cool, but I had 50+ hours of mod play/test time prior to that.

## Requirements

- **Game version**: tested on AE (1.11.221) and OG (1.10.163). Addresses for NG (1.10.984) are
  included and it should work, but has not been tested.
- [F4SE](https://www.nexusmods.com/fallout4/mods/42147)
- [MCM](https://www.nexusmods.com/fallout4/mods/21497)
- [Runtime Database](https://www.nexusmods.com/fallout4/mods/108394)
- Microsoft Visual C++ Redistributable

## Calling Control Unbinder (note for mod authors)

If your mod needs a vanilla control out of the way, you can ask Control Unbinder to do it instead
of writing to the game's control map yourself. That's how the other WattzIO mods do it. Getting
this right is fiddlier than it looks, so it's handled in one place, and every change gets logged
with the name of the mod that asked for it, which makes it much easier to work out what happened
when a player's buttons aren't what they expect.

Grab `ControlUnbinderAPI.h` from the source. It's a single header with nothing else attached to
it. At startup, look for `WIO-ControlUnbinder.dll` and ask it for the API (a normal
`GetModuleHandle` / `GetProcAddress` pair, the names are all in the header). If it isn't there,
the player just doesn't have this mod installed, so skip your feature and move on rather than
erroring out.

From there it's mostly one function. Tell it the control you care about, whether you mean gamepad
or keyboard, and whether you want it unbound or put back:

```cpp
api->Apply("ReadyWeapon", Slot::kGamepad, Action::kUnbind, "MY-CoolMod");
```

The last argument is just your mod's name, which is what ends up in the log. The call is
one-and-done: the change is saved, so you won't need to call it again on the next load. There's
no coordination between mods, so if two of them care about the same button, last one to write
wins.

The rest is for showing things to the player. There are calls for "what is this control actually
bound to right now", "what is this control called", and a ready-made status line you can drop
straight into your MCM page. They come back already translated.

Note that keyboard and mouse are a single slot as far as the game is concerned, so asking about
the keyboard covers the mouse as well.

The four D-Pad Quick Slots, the two mouse wheel zoom controls, and the twelve Favorites hotkeys
are a special case: the game marks them as not remappable and resets them every load. `Apply()`
still works on them the same way. Control Unbinder saves your request to its own settings and
reapplies it each launch, and it shows up on Control Unbinder's MCM page as if the player had set
it there. `IsInMemory` tells you whether a control is one of these. `SetPersistentUnbind` also
exists for a temporary unbind that lasts only until the game closes.

## Credits

Mod by Abbie Doobie.

Vector assets in the images are from uxwing.

## License

Licensed MIT. [Source](https://github.com/AbbieDoobie/WattzIO/tree/main/mods/WIO-ControlUnbinder)

## Changelog

**1.0.2** - Added Zoom In/Out (mouse wheel) and Favorite 1-12 to the Keyboard and Mouse page.

**1.0.1** - Fixed an issue causing the original edition of the game to crash.

**1.0.0** - Initial release.

## Building

Requires [xmake](https://xmake.io) 3.0.0 or newer and a C++23 compiler (MSVC or Clang-CL).

This mod lives in a monorepo alongside the rest of the WattzIO Fallout 4 mods, and is
built from its own directory rather than the repository root:

```bat
git clone <url-of-this-repo> wattzio
cd wattzio
git submodule update --init mods/WIO-ControlUnbinder/lib/commonlibf4rd
cd mods\WIO-ControlUnbinder
xmake f -m releasedbg
xmake
```

Every mod pins its own copy of the dependency, so cloning with `--recurse-submodules`
fetches one for all of them. Initialising just this mod's submodule is enough to build it.

The built plugin lands at `build\windows\x64\releasedbg\WIO-ControlUnbinder.dll`.

| Path | Contents |
|---|---|
| `src/` | Plugin source: header-only modules plus `main.cpp` |
| `data/` | MCM config and translation files, mirroring the game's `Data` folder |
| `lib/commonlibf4rd` | CommonLibF4RD (pinned submodule). |
| `xmake.lua` | Build configuration |

Every address the plugin resolves is marked with an `F4RD:<kind>` tag and listed in an
`F4RD RELOCATIONS` banner in the file that resolves it, so `grep -rn "F4RD RELOCATIONS" src`
enumerates them. Each banner names the kind, the site, and the id or offset resolved.
