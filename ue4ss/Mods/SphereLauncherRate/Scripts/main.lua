--------------------------------------------------------------------------------
-- SphereLauncherRate
--
-- Makes the sphere launcher firing interval configurable in Palworld (Steam,
-- v1.0.3+). Setting an interval to 0.0 restores the pre-1.0.3 spam-fire
-- behaviour (patch 1.0.3: "fixed a bug where the firing interval was not set
-- for sphere launchers").
--
-- UE4SS Lua mod for the Okaetsu Palworld fork of RE-UE4SS.
-- All logging is prefixed [SphereRate].
--
-- HOW THIS MOD WORKS
--   1. DISCOVERY (Debug = true in config.lua):
--      The exact property that 1.0.3 introduced is undocumented, so debug
--      mode registers NotifyOnNewObject on each launcher blueprint class and
--      dumps every float/double property (with its current value) plus every
--      function name on the instance to the UE4SS console. Equip and fire
--      each launcher once, then paste the dump back so the interval field
--      can be identified (expected: something like ShootInterval,
--      FireInterval, or a cooldown float).
--   2. MAIN MOD (Enabled = true):
--      On every launcher spawn, writes the configured interval to the
--      interval property. Until discovery pins the real name, a candidate
--      list is tried; once identified, set Config.IntervalPropertyName so
--      exactly one property is written.
--   3. If discovery shows the interval is enforced inside a fire function
--      rather than stored on a property, see the RegisterHook template at
--      the bottom of this file.
--
-- Everything user-reachable is wrapped in pcall: if a game update renames a
-- class/property, the mod logs an error instead of crashing the game.
--------------------------------------------------------------------------------

local TAG = "[SphereRate] "

local function log(msg)
    print(TAG .. tostring(msg) .. "\n")
end

--------------------------------------------------------------------------------
-- Configuration
--------------------------------------------------------------------------------

local DEFAULT_CONFIG = {
    Enabled = true,
    Debug = false,

    -- 0.0 = pre-1.0.3 spam-fire behaviour. Seconds between shots otherwise.
    SingleShotInterval = 0.0,
    ScatterInterval    = 0.0,
    HomingInterval     = 0.0,

    -- Leave "" to try the candidate-name list below. After discovery, set
    -- this to the real property name (e.g. "ShootInterval") so the mod
    -- writes exactly one known property.
    IntervalPropertyName = "",
}

-- config.lua lives next to this file in Scripts/ and is found via the mod's
-- package.path. package.loaded is cleared first so a UE4SS hot-reload
-- (Ctrl+R by default) re-reads the file from disk.
local function load_config()
    package.loaded["config"] = nil
    local cfg = {}
    for k, v in pairs(DEFAULT_CONFIG) do cfg[k] = v end

    local ok, user = pcall(require, "config")
    if ok and type(user) == "table" then
        for k, v in pairs(user) do
            if DEFAULT_CONFIG[k] == nil then
                log("WARNING: unknown config key '" .. tostring(k) .. "' ignored")
            else
                cfg[k] = v
            end
        end
        log("config.lua loaded (Enabled=" .. tostring(cfg.Enabled)
            .. " Debug=" .. tostring(cfg.Debug) .. ")")
    else
        log("WARNING: config.lua missing or invalid - using defaults. ("
            .. tostring(user) .. ")")
    end
    return cfg
end

local Config = load_config()

--------------------------------------------------------------------------------
-- Target launcher classes
--------------------------------------------------------------------------------

local LAUNCHERS = {
    {
        Class     = "/Game/Pal/Blueprint/Weapon/BP_SphereLauncher.BP_SphereLauncher_C",
        Label     = "single-shot sphere launcher",
        ConfigKey = "SingleShotInterval",
    },
    {
        Class     = "/Game/Pal/Blueprint/Weapon/BP_ScatterSphereLauncher.BP_ScatterSphereLauncher_C",
        Label     = "scatter sphere launcher",
        ConfigKey = "ScatterInterval",
    },
    {
        Class     = "/Game/Pal/Blueprint/Weapon/BP_HomingSphereLauncher.BP_HomingSphereLauncher_C",
        Label     = "homing sphere launcher",
        ConfigKey = "HomingInterval",
    },
}

-- Tried in order when Config.IntervalPropertyName is "". Only a property
-- that exists on the instance AND currently holds a number is written, so a
-- wrong guess is harmless. Trimmed/replaced after discovery.
local CANDIDATE_PROPERTIES = {
    "ShootInterval",
    "FireInterval",
    "FiringInterval",
    "ShotInterval",
    "AttackInterval",
    "LaunchInterval",
    "IntervalTime",
    "ShootCoolTime",
    "CoolTime",
    "CoolDownTime",
    "FireRate",
}

--------------------------------------------------------------------------------
-- Discovery: dump float/double properties + function names
--------------------------------------------------------------------------------

-- Dump once per class per session; launchers respawn on every equip and the
-- dump would otherwise flood the console.
local dumped_classes = {}

-- Property type is parsed from GetFullName() (first word, e.g.
-- "FloatProperty /Game/...:ShootInterval") rather than relying on the
-- FField class API, which has shifted between UE4SS versions.
local function property_type_and_name(prop)
    local ptype, pname
    pcall(function()
        local full = prop:GetFullName()
        if type(full) == "string" then
            ptype = full:match("^(%S+)")
        end
    end)
    pcall(function()
        pname = prop:GetFName():ToString()
    end)
    return ptype, pname
end

local function dump_instance(obj, launcher)
    log("================ DISCOVERY DUMP ================")
    log("instance of: " .. launcher.Label)
    pcall(function() log("object: " .. obj:GetFullName()) end)

    local cls
    local ok = pcall(function() cls = obj:GetClass() end)
    if not ok or cls == nil then
        log("ERROR: could not get class of instance")
        return
    end

    -- Walk the class hierarchy so inherited (native PalWeaponBase-side)
    -- properties are included, capped defensively.
    local depth = 0
    while cls and depth < 16 do
        local ok_valid, valid = pcall(function() return cls:IsValid() end)
        if not ok_valid or not valid then break end

        local cls_name = "<unknown class>"
        pcall(function() cls_name = cls:GetFullName() end)
        log("---- class: " .. cls_name)

        local prop_ok, prop_err = pcall(function()
            cls:ForEachProperty(function(prop)
                pcall(function()
                    local ptype, pname = property_type_and_name(prop)
                    if (ptype == "FloatProperty" or ptype == "DoubleProperty")
                        and pname then
                        local val = "<unreadable>"
                        pcall(function() val = tostring(obj[pname]) end)
                        log(string.format("  prop  %-40s %-15s = %s",
                            pname, ptype, val))
                    end
                end)
            end)
        end)
        if not prop_ok then
            log("  (ForEachProperty unavailable on this UE4SS build: "
                .. tostring(prop_err) .. ")")
        end

        local fn_ok, fn_err = pcall(function()
            cls:ForEachFunction(function(fn)
                pcall(function()
                    log("  func  " .. fn:GetFName():ToString())
                end)
            end)
        end)
        if not fn_ok then
            log("  (ForEachFunction unavailable on this UE4SS build: "
                .. tostring(fn_err) .. ")")
        end

        local got_super, super = pcall(function() return cls:GetSuperStruct() end)
        cls = got_super and super or nil
        depth = depth + 1
    end

    -- Fallback probe in case reflection iteration is unavailable: at least
    -- report which candidate names resolve to numbers on this instance.
    log("---- candidate-name probe:")
    for _, pname in ipairs(CANDIDATE_PROPERTIES) do
        pcall(function()
            local v = obj[pname]
            if type(v) == "number" then
                log(string.format("  candidate %-30s = %s", pname, tostring(v)))
            end
        end)
    end

    log("================ END DUMP =====================")
    log("Paste everything between the DISCOVERY DUMP markers back to identify")
    log("the interval field (likely ShootInterval / FireInterval / a cooldown"
        .. " float).")
end

--------------------------------------------------------------------------------
-- Main mod: write the configured interval on spawn
--------------------------------------------------------------------------------

local warned_no_property = {}

local function apply_interval(obj, launcher)
    local interval = tonumber(Config[launcher.ConfigKey])
    if interval == nil then
        log("ERROR: config key " .. launcher.ConfigKey
            .. " is not a number - skipping")
        return
    end
    if interval < 0 then interval = 0.0 end

    local names
    if type(Config.IntervalPropertyName) == "string"
        and Config.IntervalPropertyName ~= "" then
        names = { Config.IntervalPropertyName }
    else
        names = CANDIDATE_PROPERTIES
    end

    for _, pname in ipairs(names) do
        local ok, applied = pcall(function()
            -- Only overwrite a property that exists and holds a number;
            -- indexing a missing property raises, which pcall absorbs.
            if type(obj[pname]) == "number" then
                obj[pname] = interval
                return true
            end
            return false
        end)
        if ok and applied then
            log(string.format("%s: set %s = %.3f", launcher.Label, pname, interval))
            return
        end
    end

    if not warned_no_property[launcher.Class] then
        warned_no_property[launcher.Class] = true
        log("WARNING: no interval property found on " .. launcher.Label
            .. " (tried " .. (#names == 1 and names[1]
                or (#names .. " candidate names")) .. ").")
        log("Run discovery: set Debug = true in config.lua, fire the launcher,"
            .. " and paste the dump back. A game update may have renamed the"
            .. " field.")
    end
end

--------------------------------------------------------------------------------
-- Hook registration
--------------------------------------------------------------------------------

local function register_all()
    for _, launcher in ipairs(LAUNCHERS) do
        local ok, err = pcall(function()
            NotifyOnNewObject(launcher.Class, function(obj)
                local h_ok, h_err = pcall(function()
                    if Config.Debug then
                        local cls_key = launcher.Class
                        if not dumped_classes[cls_key] then
                            dumped_classes[cls_key] = true
                            dump_instance(obj, launcher)
                        end
                    end
                    if Config.Enabled then
                        apply_interval(obj, launcher)
                    end
                end)
                if not h_ok then
                    log("ERROR in spawn hook for " .. launcher.Label .. ": "
                        .. tostring(h_err))
                end
            end)
        end)
        if ok then
            log("hooked " .. launcher.Label)
        else
            log("ERROR: failed to register hook for " .. launcher.Class .. ": "
                .. tostring(err) .. " (game update may have moved/renamed the"
                .. " blueprint)")
        end
    end
end

register_all()

if Config.Debug then
    log("DEBUG MODE ON - discovery instructions:")
    log("  1. Load into your world.")
    log("  2. Equip and fire the single-shot, scatter, and homing sphere")
    log("     launchers once each.")
    log("  3. Copy each [SphereRate] DISCOVERY DUMP block from this console")
    log("     and paste it back so the interval field can be identified.")
end

if not Config.Enabled then
    log("Enabled = false - mod is passive (no properties will be written).")
end

log("loaded. intervals: single=" .. tostring(Config.SingleShotInterval)
    .. " scatter=" .. tostring(Config.ScatterInterval)
    .. " homing=" .. tostring(Config.HomingInterval)
    .. " (0 = pre-1.0.3 spam fire)")

--------------------------------------------------------------------------------
-- FALLBACK TEMPLATE (only if discovery shows the interval is enforced in a
-- fire/cooldown FUNCTION instead of read from a property).
--
-- Replace <FireFunctionName> with the function identified from the dump,
-- then adapt: either zero the cooldown state before the check runs, or
-- override the check's inputs. Uncomment and adjust.
--------------------------------------------------------------------------------
-- local fn = "/Game/Pal/Blueprint/Weapon/BP_SphereLauncher.BP_SphereLauncher_C:<FireFunctionName>"
-- local ok, err = pcall(function()
--     RegisterHook(fn, function(self, ...)
--         pcall(function()
--             local obj = self:get()
--             -- e.g. reset whatever timestamp/cooldown the check compares:
--             -- obj.LastShootTime = 0.0
--         end)
--     end)
-- end)
-- if not ok then
--     log("ERROR: RegisterHook(" .. fn .. ") failed: " .. tostring(err))
-- end
