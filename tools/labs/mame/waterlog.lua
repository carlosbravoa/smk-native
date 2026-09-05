-- Every frame of a race: the direct page $0080-$00FF, the player's block
-- $1000-$10FF, and four object blocks - the log NOTES 283 read the water
-- and the rescue from (tools/labs/mame/sessions/underwater1, underwater2).
--   tools/labs/mame/replay.sh underwater1 tools/labs/mame/waterlog.lua 200 > uw1.csv
-- every frame of a race: the direct page $0080-$00FF, the player's block
-- $1000-$10FF, the pad, and Lakitu's object block if one is live
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local n = 0
print("f,mode,pad,hex0080,hex1000,hex1800,hex1880,hex1900,hex1980")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036)
  if m ~= 2 and m ~= 6 then return end
  local a, b = {}, {}
  for i = 0x80, 0xFF do a[#a+1] = string.format("%02X", mem:read_u8(0x7E0000 + i)) end
  for i = 0x00, 0xFF do b[#b+1] = string.format("%02X", mem:read_u8(0x7E1000 + i)) end
  local blocks = {}
  for _, base in ipairs({0x1800, 0x1880, 0x1900, 0x1980}) do
    local c = {}
    for i = 0, 0x7F do c[#c+1] = string.format("%02X", mem:read_u8(0x7E0000 + base + i)) end
    blocks[#blocks+1] = table.concat(c)
  end
  print(string.format("%d,%02X,%04X,%s,%s,%s", n, m, mem:read_u16(0x7E0018) or 0,
    table.concat(a), table.concat(b), table.concat(blocks, ",")))
end)
