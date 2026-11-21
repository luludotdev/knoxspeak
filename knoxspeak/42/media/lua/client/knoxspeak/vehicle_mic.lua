-- client: vehicle radio mic mute sync

if isServer() then return end

require "RadioCom/ISRadioAction"

local MODULE = "knoxspeak"

local vanillaPerform = ISRadioAction.performMuteMicrophone

function ISRadioAction:performMuteMicrophone()
  vanillaPerform(self)
  if not isClient() then return end
  local dd = self.deviceData
  if not dd or not dd:isVehicleDevice() then return end
  local part = dd:getParent()
  local vehicle = part and part:getVehicle()
  if not vehicle then return end
  sendClientCommand(self.character, MODULE, "vehicleMic", {
    vehicle = vehicle:getId(),
    part = part:getId(),
    muted = dd:getMicIsMuted(),
  })
end

local function onServerCommand(module, command, args)
  if module ~= MODULE or command ~= "vehicleMic" then return end
  local vehicle = getVehicleById(args.vehicle)
  local part = vehicle and vehicle:getPartById(args.part)
  local dd = part and part:getDeviceData()
  if dd then
    dd:setMicIsMuted(args.muted == true)
  end
end

Events.OnServerCommand.Add(onServerCommand)
