-- server: vehicle radio mic mute sync

if not isServer() then return end

local MODULE = "knoxspeak"
local MAX_DIST = 5 -- tiles from the vehicle, for players operating it from outside (and lag)

local function onClientCommand(module, command, player, args)
  if module ~= MODULE or command ~= "vehicleMic" then return end
  if type(args) ~= "table" or type(args.vehicle) ~= "number" or type(args.part) ~= "string" then return end

  local vehicle = getVehicleById(args.vehicle)
  if not vehicle then return end
  if player:getVehicle() ~= vehicle then
    local dx, dy = player:getX() - vehicle:getX(), player:getY() - vehicle:getY()
    if dx * dx + dy * dy > MAX_DIST * MAX_DIST then return end
  end

  local part = vehicle:getPartById(args.part)
  local dd = part and part:getDeviceData()
  if not dd then return end

  local muted = args.muted == true
  dd:setMicIsMuted(muted)
  sendServerCommand(MODULE, "vehicleMic", { vehicle = args.vehicle, part = args.part, muted = muted })
end

Events.OnClientCommand.Add(onClientCommand)
