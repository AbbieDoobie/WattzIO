# WattzIO - Smart Heal Button - F4SE

One button that heals you fully (HP, Rads, and more) without any waste! No more burning stimpaks on
scratches, or accidentally trying to top up health that radiation has blocked.

Press it and Smart Heal looks at your health, radiation, and crippled limbs, then compares that with
everything in your inventory that can be eaten, drank, and/or injected. It works out and applies the
least wasteful set of items that gets you to where you want to be.

## Features

| What you do | What happens |
|---|---|
| Press with light damage | Eats the cheap stuff (e.g. food) |
| Press while irradiated | Clears the rads first, then fills the health they were capping |
| Press with a crippled limb | Uses a stimpak to fix the limb (if enabled) |

What this does differently from other mods and/or favoriting items manually:

- **It spends the cheap stuff first.** Missing 30 health with a stimpak and three pieces of food in
  your pack? It eats the food and keeps the stimpak.
- **It knows which food you were saving.** Food that carries a buff (e.g. Deathclaw Steak, Squirrel
  Stew, Nuka-Cola) is left alone by default.
- **It knows radiation caps your health bar.** Radiation lowers your maximum health, so healing past
  that cap is impossible. Smart Heal clears the radiation first, then fills the health the radiation
  was blocking.
- **It stops when you are where you asked to be.** Set the HP target to 80%, and it gets you as
  close to 80% as your current inventory can.
- **It reads the items, not a list of names.** Anything a mod adds is picked up automatically, as
  long as the item restores health, removes radiation, and/or mends limbs.
- **It notices your perks.** Medic makes stimpaks and RadAway do far more than the item card says.
  Smart Heal checks how you are actually doing between each item and stops as soon as you are where
  you asked to be, so the perk is accounted for whether or not the item itself knows about it. The
  same should go for anything a mod adds, though I cannot test everything!
- **RadAway is not the only thing it will use for radiation.** Anything that removes rads counts,
  including food like Mutant Hound Chops.

## Initial Setup

Open the Mod Configuration Menu and pick a button under **WattzIO - Smart Heal**. You can bind a
controller button, a keyboard key or mouse button, or both.

## Options

### Health

- **HP Target** is how full your health bar should be when Smart Heal is done. This is a percentage
  of the health you can currently reach. So for example, 100 means the top of the bar NOT including
  however much radiation is taking up. When Heal Radiation is on, the radiation gets cleared first,
  which raises that ceiling.
- **HP Hysteresis** is how close to the target counts as close enough. At 0 the target is exact. At
  5, Smart Heal may stop up to five points short rather than spend another item on it, depending on
  how things add up.
- **Always Use Stimpak if Limb is Crippled** spends one stimpak on a crippled limb even at full
  health. Note that any item that mends limbs counts.

### Radiation

- **Heal Radiation** treats radiation in the same press as health. With it off, Smart Heal fills
  only the health radiation has left you and leaves the radiation alone.
- **Max Allowed Radiation** is how much radiation (%) you are willing to be left carrying. Leave it
  at 0% to be scrubbed clean; raise it if your build wants to stay dirty.
- **Radiation Hysteresis** is the same idea as HP Hysteresis, for radiation.

### Items

- **Item Order Preference** decides which kind of item Smart Heal reaches for first. *Cheapest
  First*, the **default**, spends your least valuable items and saves the good stuff. *Prefer
  Stimpak* reaches for a stimpak whenever at least half of it would actually be put to use. *Avoid
  Stimpak* goes the other way and spends everything else first, though it will still use one when
  nothing else reaches your HP Target.
- **Max Allowed Items Per Use** caps how much one press can consume. Note that a crippled-limb
  stimpak and the reserved alcohol come out of this budget first (if either is enabled), so at the
  minimum of 2, with both turned on, you would get exactly those two and nothing else.

The rest of the page decides whether specific types of items may be spent, and if so when. Options
are included for Stimpaks, Aid, RadAway, Food, and Alcohol, as well as sub-options for stuff like
irradiated items.

### Advanced

- **HP Target Direction** and **Radiation Target Direction** decide which side of a target to land
  on when nothing in your pack hits it exactly. *Over* means overshooting is fine and falling short
  is not: at an HP Target of 80, ending on 85 is allowed and 79 is not. *Under* is the mirror. HP
  defaults to Over and radiation to Under, which is what most people want; an irradiated build will
  want to flip radiation to Over.
- **Item Source** decides which mods Smart Heal may take items from. *Base* is the original game and
  all of its official DLC; *Base + Creation Club* adds Creation Club content; *All*, the
  **default**, is everything you have installed. This goes by which mod an item came from
  originally, so a mod that rebalances a vanilla item does not remove it from the list.
- **Show What Was Used** puts a short message on screen listing what the press consumed.
- **Clean Notification Item Names** strips item sorter tags such as "[Aid]" out of that message.
  **On by default.**
- **Block Hotkeys During Menus** controls whether the button fires while the game has a prompt or
  menu showing. *Block Menus Only* (the default) blocks complex UI (e.g. Quick Loot, dialogue,
  lockpicking, VATS, Workshop, Pip-Boy), but leaves simple crosshair prompts like "A to Activate"
  alone.

## Technical Stuff and Limitations

Smart Heal should never use any of the following. If it does, please report it:

- Quest items.
- Anything with an addiction risk, aside from alcohol (if that's turned on).
- Anything whose only effect is a temporary buff. For example, Fortify Health raises your maximum
  for a while; it does not heal you, so Smart Heal does not treat it as healing.

Other things worth knowing:

- Since healing is (almost always) applied over time in FO4, Smart Heal counts healing that is
  already on its way, so pressing twice in a row shouldn't double-spend on something the first press
  already calculated for.
- The use animation is played through a small Papyrus script that comes with the mod,
  `Scripts\WIO_SmartHeal.pex`. It isn't attached to anything and needs no plugin. If it's missing,
  items are still used, just without the animation.
- I've waited to release Smart Heal because I wanted to put a lot more testing in to be sure the
  calculations and item pool are handled well. 100+ hours in total, half of that with the most
  recent code.

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

Licensed MIT. [Source](https://github.com/AbbieDoobie/WattzIO/tree/main/mods/WIO-SmartHeal)

## Changelog

Initial release.

## Building

Requires [xmake](https://xmake.io) 3.0.0 or newer and a C++23 compiler (MSVC or Clang-CL).

This mod lives in a monorepo alongside the rest of the WattzIO Fallout 4 mods, and is built from
its own directory rather than the repository root:

```bat
git clone <url-of-this-repo> wattzio
cd wattzio
git submodule update --init mods/WIO-SmartHeal/lib/commonlibf4rd
cd mods\WIO-SmartHeal
xmake f -m releasedbg
xmake
```

Every mod pins its own copy of the dependency, so cloning with `--recurse-submodules` fetches one
for all of them. Initialising just this mod's submodule is enough to build it.

The built plugin lands at `build\windows\x64\releasedbg\WIO-SmartHeal.dll`.

| Path | Contents |
|---|---|
| `src/` | Plugin source: header-only modules plus `main.cpp` |
| `data/` | MCM config, translation files, and the `WIO_SmartHeal` Papyrus script, mirroring the game's `Data` folder |
| `data/Scripts/Source/User/` | Source of `WIO_SmartHeal.pex`. The compiled `.pex` is checked in; rebuild it with the Creation Kit's Papyrus compiler after editing |
| `lib/commonlibf4rd` | CommonLibF4RD (pinned submodule) |
| `xmake.lua` | Build configuration |

Every address the plugin resolves is marked with an `F4RD:<kind>` tag and listed in an
`F4RD RELOCATIONS` banner in the file that resolves it, so `grep -rn "F4RD RELOCATIONS" src`
enumerates them. Each banner names the kind, the site, and the id or offset resolved.
