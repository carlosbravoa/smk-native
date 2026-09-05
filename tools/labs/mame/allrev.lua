-- every kart's rev $C2 and speed $EA per frame, to see how the game pitches
-- the OTHER engines (NOTES 291)
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local n = 0
print("f,k,ea,c2,ac,x10,x,y,chr")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036)
  if m ~= 2 and m ~= 6 then return end
  if n % 4 ~= 0 then return end
  for k = 0, 7 do
    local b = 0x7E1000 + k * 0x100
    print(string.format("%d,%d,%d,%d,%02X,%04X,%d,%d,%d", n, k, mem:read_u16(b + 0xEA), mem:read_u16(b + 0xC2), mem:read_u8(b + 0xAC), mem:read_u16(b + 0x10), mem:read_u16(b + 0x18), mem:read_u16(b + 0x1C), mem:read_u8(b + 0x12)))
  end
end)
