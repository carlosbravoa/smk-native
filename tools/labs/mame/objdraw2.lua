-- Are Ghost Valley's four repositioned slots actually DRAWN?
-- +$06 is the projection scale, +$30 the screen Y ($0140 = parked
-- off-screen), +$08 and +$00 whatever type/graphic the slot carries.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local n, inrace, seen = 0, false, {}
emu.register_frame_done(function()
  if mem:read_u8(0x7E0036) ~= 2 then inrace = false; return end
  if not inrace then inrace = true; n = 0 end
  n = n + 1
  if n % 60 ~= 0 or n > 1500 then return end
  local s = {}
  for i = 0, 3 do
    local b = 0x7E1800 + i * 0x80
    s[#s+1] = string.format("[+00=%04X +06=%4d +08=%04X +30=%4d]",
      mem:read_u16(b), mem:read_u16(b+6), mem:read_u16(b+8), mem:read_u16(b+0x30))
  end
  print(string.format("f%-5d %s", n, table.concat(s, " ")))
end)
