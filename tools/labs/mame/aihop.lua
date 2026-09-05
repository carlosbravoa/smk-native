-- does an AI kart ever leave the ground on plain road?  Every AI kart's
-- height $1F, state $A0, surface $68, position, plus both projectile
-- blocks raw ($1A00/$1A80: 32 bytes each), per frame (NOTES 294a)
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
print("f,k,x,y,z1F,a0,ac,surf,ea,pa,pb")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036)
  if m ~= 2 and m ~= 6 then return end
  local pa, pb = {}, {}
  for i = 0, 31 do pa[#pa+1] = string.format("%02X", mem:read_u8(0x7E1A00 + i)); pb[#pb+1] = string.format("%02X", mem:read_u8(0x7E1A80 + i)) end
  for k = 1, 7 do
    local b = 0x1000 + k * 0x100
    local z = w(b + 0x1F); local a0 = mem:read_u8(0x7E0000 + b + 0xA0)
    if z ~= 0 or a0 ~= 0 then
      print(string.format("%d,%d,%d,%d,%d,%02X,%02X,%02X,%d,%s,%s", n, k, w(b + 0x18), w(b + 0x1C), z, a0, mem:read_u8(0x7E0000 + b + 0xAC), mem:read_u8(0x7E0000 + b + 0x68), w(b + 0xEA), table.concat(pa), table.concat(pb)))
    end
  end
end)
