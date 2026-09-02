-- Every kart's 16.16 position and heading, once a frame, through a
-- recorded race.  The port prints the same rows under SMK_FIELD_TRACE, so
-- tools/labs/aiweave.py can put the game's AI and ours side by side.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n, started = 0, false
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036)
  if m ~= 2 then if started then manager.machine:exit() end return end
  started = true
  local row = {}
  for k = 0, 7 do
    local b = 0x1000 + k * 0x100
    -- $16/$18 and $1A/$1C are the fraction and the pixel of each axis
    local x = (w(b + 0x18) << 16) | w(b + 0x16)
    local y = (w(b + 0x1C) << 16) | w(b + 0x1A)
    -- $A2 is an AI kart's direction of travel and $FA its target
    row[#row+1] = string.format("%d,%d,%d,%d", x, y, w(b + 0xA2), w(b + 0xFA))
  end
  print(string.format("fp %d,%s", n, table.concat(row, ",")))
end)
