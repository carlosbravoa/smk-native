-- the sound queue and every kart's spin/drive/hit state per frame, plus
-- the player's coins, heading and camera - for the spins (NOTES 292)
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
print("f,ids,a6,ac,a0,x84,x70,coins,a4,cam94,x,y,ai_a6,ai_ac,ai_x,ai_y")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036)
  if m ~= 2 and m ~= 6 then return end
  local q = w(0x0E6A)
  local ids = {}
  if q > 0 and q <= 6 then for i = 0, q - 2, 2 do ids[#ids+1] = string.format("%02X", w(0x0E6C + i)) end end
  local ai6, aiac, aix, aiy = {}, {}, {}, {}
  for k = 1, 7 do
    local b = 0x1000 + k * 0x100
    ai6[#ai6+1] = string.format("%02X", mem:read_u8(0x7E0000 + b + 0xA6))
    aiac[#aiac+1] = string.format("%02X", mem:read_u8(0x7E0000 + b + 0xAC))
    aix[#aix+1] = tostring(w(b + 0x18)); aiy[#aiy+1] = tostring(w(b + 0x1C))
  end
  print(string.format("%d,%s,%02X,%02X,%02X,%04X,%04X,%d,%04X,%04X,%d,%d,%s,%s,%s,%s", n, table.concat(ids, "+"),
    mem:read_u8(0x7E10A6), mem:read_u8(0x7E10AC), mem:read_u8(0x7E10A0), w(0x1084), w(0x1070), w(0x0E00), w(0x10A4), w(0x0094), w(0x1018), w(0x101C),
    table.concat(ai6, " "), table.concat(aiac, " "), table.concat(aix, " "), table.concat(aiy, " ")))
end)
