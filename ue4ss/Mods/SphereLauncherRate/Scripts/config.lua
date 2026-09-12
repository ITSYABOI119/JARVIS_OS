--------------------------------------------------------------------------------
-- SphereLauncherRate configuration
--
-- Intervals are in SECONDS between shots.
--   0.0 = pre-1.0.3 spam-fire behaviour (fire as fast as you can trigger).
--
-- Values are read at game launch. If UE4SS hot-reload is enabled (Ctrl+R by
-- default), edits to this file are picked up on reload; already-spawned
-- launchers update the next time they spawn (re-equip the weapon).
--------------------------------------------------------------------------------

return {
    -- Master switch. false = mod loads but writes nothing.
    Enabled = true,

    -- Discovery mode: dumps all float/double properties + function names of
    -- each launcher to the UE4SS console on first spawn, prefixed
    -- [SphereRate]. Equip and fire each launcher once, then paste the dumps
    -- back to identify the interval field.
    Debug = false,

    -- BP_SphereLauncher (single-shot)
    SingleShotInterval = 0.0,

    -- BP_ScatterSphereLauncher
    ScatterInterval = 0.0,

    -- BP_HomingSphereLauncher
    HomingInterval = 0.0,

    -- Leave "" to auto-try a list of likely property names. After discovery
    -- identifies the real one (e.g. "ShootInterval"), put it here so the mod
    -- writes exactly that property and nothing else.
    IntervalPropertyName = "",
}
