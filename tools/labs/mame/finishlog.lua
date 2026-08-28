-- What the GAME does when the player crosses the line.
--
-- The port's finish sequence was designed rather than measured (S27),
-- because no gate saw a finish.  It turns out the user's own recorded
-- races run past the flag, so the real camera move and the real
-- celebration are sitting in them.
--
-- Rather than guess which addresses matter, dump two whole pages every
-- frame from a little before the crossing: the direct-page globals
-- $0080-$00FF (the camera azimuth is $0094 - "cam $94 - $A4 == 192 every
-- frame", NOTES 083) and the player's own block $1000-$10FF.  Diffing
-- them across the crossing says what moved, without a hypothesis.
--
--   tools/labs/mame/replay.sh flag tools/labs/mame/finishlog.lua 400
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n, armed = 0, false
print("f,lapbyte,hex0080,hex1000")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  -- $C1 is the lap counter, based at $7F (NOTES 174); $85 is the finish
  local lap = mem:read_u8(0x7E10C1)
  if lap >= 0x84 then armed = true end
  if not armed then return end
  local a, b = {}, {}
  for i = 0x80, 0xFF do a[#a+1] = string.format("%02X", mem:read_u8(0x7E0000 + i)) end
  for i = 0x00, 0xFF do b[#b+1] = string.format("%02X", mem:read_u8(0x7E1000 + i)) end
  print(string.format("%d,%02X,%s,%s", n, lap, table.concat(a), table.concat(b)))
end)
