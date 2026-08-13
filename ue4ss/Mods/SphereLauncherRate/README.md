# SphereLauncherRate

A UE4SS Lua mod for **Palworld (Steam, v1.0.3+)** that makes the sphere
launcher firing interval configurable — including `0.0`, which restores the
pre-1.0.3 spam-fire behaviour (patch 1.0.3 "fixed a bug where the firing
interval was not set for sphere launchers").

Covered weapons (all under `/Game/Pal/Blueprint/Weapon/`):

| Blueprint | Config key |
|---|---|
| `BP_SphereLauncher` (single-shot) | `SingleShotInterval` |
| `BP_ScatterSphereLauncher` | `ScatterInterval` |
| `BP_HomingSphereLauncher` | `HomingInterval` |

This is a **runtime Lua mod**, not a `.pak` blueprint edit — intervals are
read from a config file, no repacking needed.

## Prerequisite: UE4SS (Okaetsu Palworld fork)

Palworld needs the Okaetsu fork of RE-UE4SS (mainline UE4SS releases do not
track Palworld's engine updates):

- https://github.com/Okaetsu/RE-UE4SS/releases

Install UE4SS per its own instructions into:

```
<Steam>\steamapps\common\Palworld\Pal\Binaries\Win64\
```

so that `Pal\Binaries\Win64\ue4ss\` exists and the game boots with the UE4SS
console available.

## Install

Copy the `SphereLauncherRate` folder into the UE4SS `Mods` directory:

```
Palworld\Pal\Binaries\Win64\ue4ss\Mods\SphereLauncherRate\
├── enabled.txt
└── Scripts\
    ├── main.lua
    └── config.lua
```

`enabled.txt` (empty file) tells UE4SS to load the mod. If your UE4SS build
uses `Mods\mods.txt` instead, add a line: `SphereLauncherRate : 1`.

On launch, the UE4SS console should show lines prefixed `[SphereRate]`,
ending with `loaded. intervals: ...`.

> Older UE4SS builds place `Mods\` directly in `Win64\` rather than under
> `Win64\ue4ss\`. Use whichever `Mods` folder your UE4SS install created.

## Configure

Edit `Scripts\config.lua`:

```lua
return {
    Enabled = true,          -- master switch
    Debug   = false,         -- discovery dump mode (see below)

    SingleShotInterval = 0.0, -- seconds between shots; 0.0 = pre-1.0.3 spam fire
    ScatterInterval    = 0.0,
    HomingInterval     = 0.0,

    IntervalPropertyName = "", -- "" = auto-detect; set after discovery
}
```

Values are read at game launch. With UE4SS hot-reload (**Ctrl+R** by
default) config edits are re-read on reload; a launcher you're already
holding picks the new value up the next time it spawns (unequip/re-equip).

## First run: discovery mode

The exact property 1.0.3 introduced is undocumented, so the mod ships with a
discovery mode plus a candidate-name list (`ShootInterval`, `FireInterval`,
cooldown floats, …). To pin the real name:

1. Set `Debug = true` in `config.lua` and launch the game.
2. Equip and fire **each** of the three launchers once.
3. Copy each `[SphereRate] DISCOVERY DUMP` block from the UE4SS console
   (every float/double property with its value, plus all function names) and
   paste it back for identification.
4. Set `IntervalPropertyName = "<the real name>"` and `Debug = false`.

Until then the mod tries the candidate list; it only writes a property that
already exists and holds a number, so a wrong guess is harmless. If the
interval turns out to be enforced inside a fire function rather than stored
on a property, `main.lua` contains a ready `RegisterHook` template for that
case (bottom of the file).

## Scope

- **Client-side.** Works in single-player and as a co-op **host** (weapon
  actors live on the host). Joining someone else's world, the host's
  behaviour wins.
- **Dedicated servers: untested.** The hook would need to run server-side
  and the launcher blueprints may behave differently there — flagging as
  unknown, not supported.
- A Palworld update that renames the blueprints or the interval property
  will not crash the game: everything is pcall-wrapped and failures are
  logged to the UE4SS console as `[SphereRate] ERROR/WARNING ...`. Re-run
  discovery to find the new name.

## Uninstall

Delete the folder:

```
Palworld\Pal\Binaries\Win64\ue4ss\Mods\SphereLauncherRate\
```

(or just delete its `enabled.txt` / set the `mods.txt` entry to `0` to
disable while keeping it installed). Nothing is patched on disk; removing
the mod fully restores stock behaviour.
