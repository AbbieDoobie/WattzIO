# WattzIO - Radio Notification Filter - F4SE

Disable "signal found" and "signal lost" notifications from radios coming in and out of range.

This mod adds options to hide radio station and settlement beacon signal notifications, split into separate toggles for found and lost (making 4 toggles total). I have a lot of settlements, and get tired of the notifications. By default this disables the "signal lost" notification for both radio stations and settlement beacons.

## Features

| What happens in game | What you see |
|---|---|
| Radio station comes into range | Shown by default (turn on Hide Station Found to hide) |
| Radio station drops out of range | Hidden by default |
| Settlement beacon comes into range | Show by default (turn on Hide Settlement Beacon Found to hide) |
| Settlement beacon drops out of range | Hidden by default |

## Initial Setup

Open the Mod Configuration Menu and find **WattzIO - Radio Notifications**. Changes take effect as soon as you close the pause menu.

## Options

**Radio Stations**

- **Hide Station Found** - **off by default.**
- **Hide Station Lost** - **on by default.**

**Settlement Beacons**

- **Hide Settlement Beacon Found** - **off by default.**
- **Hide Settlement Beacon Lost** - **on by default.**

## Technical Stuff and Limitations

- Quest radio signals count as stations. That includes distress calls, the
  Courser signal, and similar one-off broadcasts, so hiding Station Found will hide those too. I strongly recommend not doing that!
- Nothing is changed permanently, so if you uninstall, you'll get all notifications again.
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

Mod by Abbie Doobie. Credit to Magicockerel (No Radio Station Notifications) for pointers on radio
notifications.

Vector assets in the images are from uxwing.

## License

Licensed MIT. [Source](https://github.com/AbbieDoobie/WattzIO/tree/main/mods/WIO-RadioNotifications)

## Changelog

Initial release.

## Building

Requires [xmake](https://xmake.io) 3.0.0 or newer and a C++23 compiler (MSVC or Clang-CL).

This mod lives in a monorepo alongside the rest of the WattzIO Fallout 4 mods, and is
built from its own directory rather than the repository root:

```bat
git clone <url-of-this-repo> wattzio
cd wattzio
git submodule update --init mods/WIO-RadioNotifications/lib/commonlibf4rd
cd mods\WIO-RadioNotifications
xmake f -m releasedbg
xmake
```

Every mod pins its own copy of the dependency, so cloning with `--recurse-submodules`
fetches one for all of them. Initialising just this mod's submodule is enough to build it.

The built plugin lands at `build\windows\x64\releasedbg\WIO-RadioNotifications.dll`.

| Path | Contents |
|---|---|
| `src/` | Plugin source: header-only modules plus `main.cpp` |
| `data/` | MCM config and translation files, mirroring the game's `Data` folder |
| `lib/commonlibf4rd` | CommonLibF4RD (pinned submodule). |
| `xmake.lua` | Build configuration |

Every address the plugin resolves is marked with an `F4RD:<kind>` tag and listed in an
`F4RD RELOCATIONS` banner in the file that resolves it, so `grep -rn "F4RD RELOCATIONS" src`
enumerates them. Each banner names the kind, the site, and the id or offset resolved.
