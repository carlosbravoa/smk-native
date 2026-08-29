-- The whole object window, every frame: $1800..$1BFF as hex words, plus
-- every kart's position/flags/$E0/$E2.  Post-processed offline.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  local o = {n}
  for a = 0x1C00, 0x1FFE, 2 do o[#o+1] = string.format("%04X", w(a)) end
  for q = 0, 7 do
    local kb = 0x1000 + q * 0x100
    o[#o+1] = string.format("%d/%d/%04X/%04X/%04X/%04X", w(kb+0x18), w(kb+0x1C), w(kb+0x10), w(kb+0xE0), w(kb+0xE2), w(kb+0x14))
  end
  print(table.concat(o, " "))
end)
