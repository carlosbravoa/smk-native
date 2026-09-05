-- The rev $C2 against the speed $EA, the drive and hazard states and the
-- pad, per frame, from a recording - the log NOTES 285 read the wall and
-- contact halvings from:
--   tools/labs/mame/replay.sh cc150 tools/labs/mame/revspeed.lua 130 > rev.csv
-- the player's rev $C2, speed $EA, the note handed to the chip $42, the
-- states, the pad, per frame
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local n = 0
print("f,mode,ea,c2,n42,ac,a0,e2,c4,ee,d6,fc,x,y,lap")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036)
  if m ~= 2 and m ~= 6 then return end
  local function w(a) return mem:read_u16(0x7E1000 + a) end
  print(string.format("%d,%02X,%d,%d,%d,%02X,%02X,%04X,%04X,%d,%d,%d,%d,%d,%02X", n, m,
    w(0xEA), w(0xC2), w(0x42), mem:read_u8(0x10AC), mem:read_u8(0x10A0), w(0xE2), w(0xC4),
    (w(0xEE) >= 32768) and (w(0xEE) - 65536) or w(0xEE), w(0xD6), w(0xFC), w(0x18), w(0x1C), mem:read_u8(0x10C1)))
end)
