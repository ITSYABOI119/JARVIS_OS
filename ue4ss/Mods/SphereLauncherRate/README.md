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

## Install — step by step

### 1. Find your Palworld folder

In Steam: right-click **Palworld** in your library → **Manage** →
**Browse local files**. That opens the game folder, typically:

```
C:\Program Files (x86)\Steam\steamapps\common\Palworld\
```

From there, navigate into `Pal\Binaries\Win64\`. Everything below happens
inside that `Win64` folder.

### 2. Install UE4SS first (one-time)

If you haven't already: download the latest zip from the Okaetsu fork's
releases page (link above) and extract it directly into `Win64\`, so that
`Win64\dwmapi.dll` and the `Win64\ue4ss\` folder exist. Start the game once
— a **UE4SS console window** (separate black log window) should appear
alongside the game. If it does, UE4SS is working; close the game.

> If no console appears, open `Win64\ue4ss\UE4SS-settings.ini` and make sure
> `ConsoleEnabled = 1` (and/or `GuiConsoleEnabled = 1`) under `[Debug]`.
> You'll want the console visible for this mod's discovery mode and logs.

### 3. Copy this mod in

Copy the **entire `SphereLauncherRate` folder** from this repo
(`ue4ss/Mods/SphereLauncherRate/`) into the UE4SS `Mods` folder, so you end
up with:

```
Palworld\Pal\Binaries\Win64\ue4ss\Mods\SphereLauncherRate\
├── enabled.txt
├── README.md            (optional, harmless)
└── Scripts\
    ├── main.lua
    └── config.lua
```

Copy the folder itself, not just its contents — the path must contain
`\Mods\SphereLauncherRate\Scripts\main.lua` exactly, or UE4SS won't find it.

> Older UE4SS builds place `Mods\` directly in `Win64\` rather than under
> `Win64\ue4ss\`. Use whichever `Mods` folder your UE4SS install created —
> the right one already contains folders like `shared\` and `Keybinds\`.

### 4. Enable it

`enabled.txt` (an empty file, already included) tells UE4SS to load the mod
— nothing more to do. If your UE4SS build instead uses a `Mods\mods.txt`
list, add this line to it:

```
SphereLauncherRate : 1
```

### 5. Launch and verify

Launch Palworld normally through Steam (UE4SS injects itself; there is no
separate launcher). In the UE4SS console window, look for lines prefixed
`[SphereRate]` — a healthy load looks like:

```
[SphereRate] config.lua loaded (Enabled=true Debug=false)
[SphereRate] 3 launcher class(es) not loaded yet (normal at startup) - hooks will register automatically once they load
[SphereRate] loaded. intervals: single=0 scatter=0 homing=0 (0 = pre-1.0.3 spam fire)
```

The "not loaded yet" line is expected: the launcher blueprint classes only
load the first time such a weapon spawns, so the mod defers its hooks and
retries automatically (on player spawn and via a cheap 3-second poll) — no
action needed.

Then load a save, equip a sphere launcher, and fire. Shortly after the
weapon spawns you should see:

```
[SphereRate] hooked single-shot sphere launcher
[SphereRate] single-shot sphere launcher: set <property> = 0.000
```

and spheres should throw as fast as you can trigger. (The first-ever
launcher of each type may take up to one 3-second poll tick before the hook
lands and applies — the mod sweeps already-spawned instances when it does,
so the weapon in your hand is covered.)

**If you see no `[SphereRate]` lines at all**, the mod isn't loading — check
step 3's folder layout and step 4. **If you see a `WARNING: no interval
property found`**, the mod loaded but couldn't auto-detect the interval
field — run discovery (next section).

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
