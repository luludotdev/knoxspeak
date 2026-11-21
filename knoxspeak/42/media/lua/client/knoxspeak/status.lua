-- status ui

if isServer() then return end

require "ISUI/ISPanel"

local RED = { r = 1.0, g = 0.35, b = 0.3 }
local ORANGE = { r = 1.0, g = 0.7, b = 0.25 }

local function statusLine()
  if not KnoxSpeak_isPluginConnected then
    return { text = "knoxspeak: Java mod not loaded. (is ZombieBuddy running and the mod approved?)", color = RED }
  end
  if not KnoxSpeak_isPluginConnected() then
    return {
      text = "knoxspeak: TeamSpeak plugin not running (start TeamSpeak 3 with the knoxspeak plugin enabled)",
      color = RED
    }
  end
  if not KnoxSpeak_isTsConnected() then
    return { text = "knoxspeak: TeamSpeak is not connected to a server", color = ORANGE }
  end
end

KnoxSpeakStatus = ISPanel:derive("KnoxSpeakStatus")

local WIDTH = 900
local TOP = 40

function KnoxSpeakStatus:new()
  local o = ISPanel.new(self, (getCore():getScreenWidth() - WIDTH) / 2, TOP, WIDTH, 60)
  o.background = false
  o.border = false
  o:instantiate()
  o.javaObject:setConsumeMouseEvents(false)
  o:setAlwaysOnTop(true)
  return o
end

function KnoxSpeakStatus:prerender() end

function KnoxSpeakStatus:render()
  local line = statusLine()
  if not line then return end
  local x = (self.width - getTextManager():MeasureStringX(UIFont.Small, line.text)) / 2
  self:drawText(line.text, x + 1, 1, 0, 0, 0, 0.8, UIFont.Small)
  self:drawText(line.text, x, 0, line.color.r, line.color.g, line.color.b, 1, UIFont.Small)
end

local panel = nil

local function onGameStart()
  if not isClient() or panel then return end
  panel = KnoxSpeakStatus:new()
  panel:addToUIManager()
end

local function onResolutionChange(_, _, newW, _)
  if panel then
    panel:setX((newW - WIDTH) / 2)
  end
end

Events.OnGameStart.Add(onGameStart)
Events.OnResolutionChange.Add(onResolutionChange)
