-- Every kart block, every frame ($1000..$17FF as hex words): the AI's one
-- item, if it lives in the kart's own block, shows up as words that
-- change rarely and look like the road.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  local o = {n}
  for a = 0x1000, 0x17FE, 2 do o[#o+1] = string.format("%04X", w(a)) end
  print(table.concat(o, " "))
end)
