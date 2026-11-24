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

local TOP = 40

function KnoxSpeakStatus:new()
  local o = ISPanel.new(self, 0, TOP, 1, 1)
  o.background = false
  o.border = false
  o.line = nil
  o:instantiate()
  o.javaObject:setConsumeMouseEvents(false)
  o:setAlwaysOnTop(true)
  o:setVisible(false)
  return o
end

function KnoxSpeakStatus:refresh()
  local line = statusLine()
  self.line = line
  if not line then
    if self:isVisible() then self:setVisible(false) end
    return
  end
  local w = getTextManager():MeasureStringX(UIFont.Small, line.text) + 1
  local h = getTextManager():getFontHeight(UIFont.Small) + 1
  self:setWidth(w)
  self:setHeight(h)
  self:setX((getCore():getScreenWidth() - w) / 2)
  if not self:isVisible() then self:setVisible(true) end
end

function KnoxSpeakStatus:prerender() end

function KnoxSpeakStatus:render()
  local line = self.line
  if not line then return end
  self:drawText(line.text, 1, 1, 0, 0, 0, 0.8, UIFont.Small)
  self:drawText(line.text, 0, 0, line.color.r, line.color.g, line.color.b, 1, UIFont.Small)
end

-- stop eating mouse inputs on the status ui
function KnoxSpeakStatus:onRightMouseDown() return false end

function KnoxSpeakStatus:onRightMouseUp() return false end

function KnoxSpeakStatus:onMouseDown() return false end

function KnoxSpeakStatus:onMouseUp() return false end

local panel = nil

local function onGameStart()
  if not isClient() or panel then return end
  panel = KnoxSpeakStatus:new()
  panel:addToUIManager()
end

local function onTick()
  if panel then panel:refresh() end
end

Events.OnGameStart.Add(onGameStart)
Events.OnTick.Add(onTick)
