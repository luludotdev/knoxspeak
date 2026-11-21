-- drive the java mod on-tick handler and settings

if isServer() then return end

KnoxSpeak = KnoxSpeak or {}

local warned = false
local SETTINGS_EVERY = 120 -- game ticks (~2 s)
local ticks = SETTINGS_EVERY

local function pushSettings()
  local opts = KnoxSpeak.options
  if not opts then return end
  KnoxSpeak_setClientOptions(opts.radioFilter(), opts.radioStatic(), opts.hearingTraits())
  KnoxSpeak_setHearing(opts.voiceRange(), opts.muffleStrength(), opts.falloff())
end

local function onTick()
  if KnoxSpeak_onTick then
    ticks = ticks + 1
    if ticks >= SETTINGS_EVERY then
      ticks = 0
      pushSettings()
    end
    KnoxSpeak_onTick()
  elseif not warned then
    warned = true
    print("[knoxspeak] Java mod not loaded. Ensure ZombieBuddy is installed and the plugin is active.")
  end
end

Events.OnTick.Add(onTick)
