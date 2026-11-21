-- options menu

if isServer() then return end

KnoxSpeak = KnoxSpeak or {}

local MODE_HOLD = 2 -- muteMode combo: 1 = Toggle, 2 = Hold

local page = PZAPI.ModOptions:create("knoxspeak", "knoxspeak")

page:addTitle("TeamSpeak Microphone")
page:addDescription("Mutes your TeamSpeak microphone from in game.")

local muteKey = page:addKeyBind(
  "muteKey",
  "Mute Microphone",
  0, -- unbound
  "Key that mutes your TeamSpeak microphone. Unbound by default."
)

local muteMode = page:addComboBox(
  "muteMode",
  "Mute Behaviour",
  "Toggle: press to mute, press again to unmute.\nHold: muted while the key is held."
)
muteMode:addItem("Toggle", true)
muteMode:addItem("Hold", false)

page:addTitle("Hearing")

local voiceRange = page:addSlider(
  "voiceRange",
  "Voice Range (tiles)",
  5,   -- min
  100, -- max
  1,   -- step
  30,  -- default
  "How far an unobstructed voice carries before it fades to silence."
)

local DEFAULT_FALLOFF = 0.5

local falloff = page:addTextEntry(
  "falloff",
  "Falloff Strength (0-1)",
  tostring(DEFAULT_FALLOFF), -- default
  "How quickly voices get quieter with distance. 0.5 = default.\nHigher: voices drop off sooner. Lower: voices stay loud until near Voice Range."
)

local muffleStrength = page:addSlider(
  "muffleStrength",
  "Muffle Strength (percent)",
  0,   -- min
  200, -- max
  5,   -- step
  100, -- default
  "Scales all muffling from walls, doors, windows, rooms and floors.\n100 = default, 0 = none, 200 = double."
)

local hearingTraits = page:addTickBox(
  "hearingTraits",
  "Hearing Traits Affect Voice",
  true, -- default
  "Hard of Hearing: shorter voice range, voices always a little muffled, radios quieter.\nDeaf: you only hear people within 2 tiles that you can see (lip reading), and no radios."
)

page:addTitle("Radio") -- seesaw

local radioFilter = page:addTickBox(
  "radioFilter",
  "Radio Filter",
  true, -- default
  "Voices heard over a radio sound tinny and slightly distorted, more so near the edge of range."
)

local radioStatic = page:addTickBox(
  "radioStatic",
  "Radio Static",
  true, -- default
  "Background static on voices heard over a radio.\nWith or without these, voices still fade out near the edge of range."
)

KnoxSpeak.options = {
  muteKey = function() return muteKey:getValue() or 0 end,
  isHoldToMute = function() return muteMode:getValue() == MODE_HOLD end,
  radioFilter = function() return radioFilter:getValue() ~= false end,
  radioStatic = function() return radioStatic:getValue() ~= false end,
  hearingTraits = function() return hearingTraits:getValue() ~= false end,
  voiceRange = function() return voiceRange:getValue() or 30 end,
  muffleStrength = function() return muffleStrength:getValue() or 100 end,
  falloff = function() return tonumber(falloff:getValue()) or DEFAULT_FALLOFF end,
}
