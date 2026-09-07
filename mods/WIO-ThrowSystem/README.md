# WattzIO - Throwing System Overhaul (TSO) - F4SE

Prevent unintentional throws, save/hotkey/cycle your favorite throwables, restock grenades on kill,
bind actions (Throw, Melee, Cycling, etc), and more. Augments the stock throwing system to feel more
inline with modern games, without modifying existing behaviors. Should be compatible with everything.

V2 is a full F4SE DLL plugin rewrite, no ESP needed! Proper engine-based Throw and Melee hotkeys are here
too. If you have the original V1 mod installed, read **Upgrading from an older version** below before
upgrading.

## Features

- **Prevent unintentional throws** - Options to check whether your weapon is drawn before starting a throw,
  and whether your weapon is blocked, relaxed, and/or reloading.
- **Engine or Animation-only Throw/Melee** - Engine mode drives the vanilla throw and melee logic, so
  you can cook/arc nades and parry/special-kill enemies. Animation-only is simpler, and the throw in
  particular does feel more snappy. Animation-only is also a good fallback from Engine mode if a major
  game update ever breaks the hooks being used for it.
- **Quick Slots** - Favorites, but for grenades and mines.
- **Cycling** - Rotate through Quick Slots on a hotkey, with an option to search for any throwable (even
  if it isn't on a Quick Slot) as a fallback.
- **Full control options** For keyboard, mouse, and gamepad. Throw, Melee, Quick Slots and Cycling are
  all bindable.
- **Auto-Equip** - Equips a throwable automatically if nothing is equipped when a throw starts.
- **Restock on Kill** - Gain throwables on kill (if enabled). By default, you need to approach the body to
  "pickup" the item.
- **Notifications** - Configurable messages for all of the above.
- **Optional vanilla key unbind** - Frees up the vanilla Bash key (combined Melee/Throw).

Most features are **off by default**. Go through the MCM pages and turn on what you want.

## Initial Setup

Open the Mod Configuration Menu, find **WattzIO - Throw System**, and set your Throw and Melee keys
on the General Settings page.

After that, everything else is opt-in. The pages are laid out roughly in the order you'd want to
work through them.

## Options

| Page | Contents |
|---|---|
| General Settings | No more accidental throws. Bind Throw (as well as Melee), and choose pre-throw checks. |
| Cycling and Quick Slots | Favorite-like slots for throwable items, and a cycling/rotation system. |
| Search and Equip | Automatically find and equip throwables. |
| Restock on Kill | Gain throwables on kill. Bypass crafting/purchasing of your favorites to streamline gameplay. |
| Notifications | Adjust notifications/messages. |
| Advanced | Throw/Melee input modes, hotkey blocking in menus, and item detection keywords. |

## Upgrading from an older version

If you have a save with an earlier, ESL-based version of this mod already loaded, install the
separate, optional **Transition** download first and let the game load once with it active before
removing the old file. That gracefully stops V1's internal script state. New installs with no
history of this mod don't need to use it.

The majority of your MCM settings carry over automatically when you launch with V2, however **on
that first launch, you must load a save, then close and reopen the game.** This allows the migration
to finish.

## Technical Stuff and Limitations

- Compatible with any and all mods in theory, even those that touch throwables or throwing
  animations. TSO does not edit vanilla records, apply spells or effects, or touch existing systems.
- V1 is fully compatible with FOLON / Fallout London. Development actually started when FOLON 1.03
  released, just to stop me throwing grenades at NPCs by accident. V2 (F4SE DLL) should be compatible in
  theory (see Requirements section).
- In Engine mode, a throw won't interrupt a melee that is still swinging. Press again once the
  animation finishes. The press is ignored rather than turned into another melee swing.
  Animation-only mode isn't affected.
- Interface translation files live under `Data/Interface/Translations/`, currently English only.
  Every notification the mod shows is a line in there, so they can be reworded or translated
  without touching anything else.
- No ESP, no ESL, no Papyrus. Pure native F4SE plugin, configured entirely through MCM.
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
  Only needed for the MCM option that removes the vanilla Melee/Throw mapping (to free up the button).

## Credits

Mod by Abbie Doobie. Credit to jarari (Melee and Throw) for info on separating throw/melee, and
shad0wshayd3 (Baka Framework) for the EditorID lookup technique.

Vector assets in the images are from uxwing.

## License

Licensed MIT. [Source](https://github.com/AbbieDoobie/WattzIO/tree/main/mods/WIO-ThrowSystem)

## Changelog

### 2.0.1

Fixed an issue causing the game to crash on the original runtime (1.10.163).

### 2.0.0

Rebuilt from the ground up as a native F4SE plugin. Version 1.x was Papyrus and an ESL.

- No ESP, ESL, Quest or Papyrus of any kind. Garden of Eden Papyrus Extender and Papyrus Common
  Library are no longer required.
- Throw and Melee each choose Engine or Animation-only. Engine drives the vanilla handler, so
  cooking, arcs, parries and special kills still work from a dedicated key.
- Auto-Equip on an empty throw replaces the old separate cycle-on-empty and search-on-empty
  options, and can throw immediately after equipping.
- Quick Slots gained Clear on Hold, and Throw After Equipping now works.
- Restock on Kill gained Pickup on Approach, where the reward waits on the body until you walk
  over to it.
- Hotkey blocking during menus is a three-way choice, so an ordinary crosshair prompt no longer
  counts as a menu.
- Unbinding the vanilla Melee/Throw key goes through WattzIO - Control Unbinder and can be undone.
- Settings carry over from 1.x automatically. Keyboard hotkeys need setting once more.

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

The built plugin lands at `build\windows\x64\releasedbg\WIO-ThrowSystem.dll`.

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
