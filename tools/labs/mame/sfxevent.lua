-- Every sound request WITH the state around it, so an id can be named
-- by what the game was doing rather than by ear: speed, the drive and
-- pose states, the hazard, the surface class, coins, the item word and
-- the lap counter, every frame.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function b(a) return mem:read_u8(0x7E0000 + a) end
local n = 0
print("frame,ids,speed,drive,pose,hazard,surf,coins,item,lap,z,mode,slideA8,slideAA,flags4E")
emu.register_frame_done(function()
  n = n + 1
  local m = b(0x36) // 2
  if m ~= 6 and m ~= 1 then return end
  local ids = {}
  local q = w(0x0E6A)
  if q > 0 and q <= 6 then
    for i = 0, q - 2, 2 do ids[#ids+1] = string.format("%02X", w(0x0E6C + i)) end
  end
  print(string.format("%d,%s,%d,%02X,%02X,%02X,%02X,%d,%04X,%d,%04X,%d,%04X,%04X,%04X",
    n, table.concat(ids, "+"), w(0x10EA), b(0x10AC), b(0x10A6), b(0x10A0),
    b(0x1068), w(0x0E00), w(0x0D70), w(0x10C0), w(0x101E), m,
    w(0x10A8), w(0x10AA), w(0x104E)))
end)
