-- the player's whole block, every 4 frames, over a window: for finding
-- the fields a poison mushroom changes
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local f0, f1 = tonumber(os.getenv("F0") or "1400"), tonumber(os.getenv("F1") or "2400")
local out = io.open(os.getenv("P1DUMP") or "p1dump.bin", "wb")
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n < f0 or n > f1 or (n % 2) ~= 0 then return end
  local s = {string.pack("<I4", n)}
  for a = 0x1000, 0x10FF, 2 do s[#s+1] = string.pack("<I2", mem:read_u16(0x7E0000 + a)) end
  for a = 0x0D70, 0x0D7F, 2 do s[#s+1] = string.pack("<I2", mem:read_u16(0x7E0000 + a)) end
  out:write(table.concat(s))
  if n >= f1 then out:close(); manager.machine:exit() end
end)
