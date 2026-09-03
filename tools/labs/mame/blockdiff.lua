-- Every word of one kart block, every frame of a window - to see which
-- field fires an AI special (docs/ITEMS.md: the spawner is $80:F17A).
--   KART=k FROM=f TO=f
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local K, FROM, TO = tonumber(os.getenv("KART") or "7"), tonumber(os.getenv("FROM")), tonumber(os.getenv("TO"))
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n < FROM then return end
  if n > TO then manager.machine:exit(); return end
  local b, o = 0x7E1000 + K * 0x100, {}
  for i = 0, 0xFE, 2 do o[#o+1] = string.format("%04X", mem:read_u16(b + i)) end
  print(string.format("f%d %s", n, table.concat(o, " ")))
end)
