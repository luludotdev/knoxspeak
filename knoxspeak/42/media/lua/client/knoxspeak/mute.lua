-- mic mute input handling

if isServer() then return end

KnoxSpeak = KnoxSpeak or {}

local restoreAfterHold = nil -- hold mode: the mute state to go back to on release
local heldKey = nil

-- mouse inputs are polled as zomboid fires pressed/released on the same tick for them
local function boundKey(key)
  local opts = KnoxSpeak.options
  if not opts or not isClient() or not KnoxSpeak_setMicMuted then return false end
  local k = opts.muteKey()
  return k ~= 0 and key == k
end

local function onKeyDown(key)
  if not boundKey(key) then return end
  if KnoxSpeak.options.isHoldToMute() then
    if restoreAfterHold == nil then
      restoreAfterHold = KnoxSpeak_isMicMuted()
      heldKey = key
      KnoxSpeak_setMicMuted(true)
    end
  else
    KnoxSpeak_setMicMuted(not KnoxSpeak_isMicMuted())
  end
end

local function stillHeld(key)
  if key >= 10000 then
    -- ui panels eat mouse inputs, hack to fix
    return isMouseButtonDown(key - 10000)
  end
  return isKeyDown(key)
end

local function onTick()
  if restoreAfterHold ~= nil and not stillHeld(heldKey) then
    -- stay muted if muted before holding input
    KnoxSpeak_setMicMuted(restoreAfterHold)
    restoreAfterHold = nil
    heldKey = nil
  end
end

Events.OnKeyStartPressed.Add(onKeyDown)
Events.OnTick.Add(onTick)
