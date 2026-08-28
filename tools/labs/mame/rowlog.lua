-- Every input of $80ADA0, the AI row chooser, and the row it produced.
--
-- The routine returns $00/$08/$10/$18 into $C8 and branches on: $84 and
-- $10 bit 5 (the "in trouble" test), $E2 bit 1 (which policy), $E6 (rank
-- x2), the rank->kart tables at $010C/$0110, the kart ahead's own $C8,
-- $DA on both karts, and two DSP-1 distances cached in $90/$92 with
-- their partners in $94/$96.  Logging all of them per frame lets the
-- ported routine be diffed against the original frame by frame, instead
-- of being judged by how the race feels (NOTES 174).
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
local h = {"f"}
for i = 0, 9 do h[#h+1] = string.format("t%03X", 0x010C + i * 2) end
for k = 0, 7 do
  local p = "k" .. k
  for _, f in ipairs({"C8","DA","E2","E6","10","84","90","92","94","96","C1","spd","x","y"}) do
    h[#h+1] = p .. f
  end
end
print(table.concat(h, ","))
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  local o = {n}
  for i = 0, 9 do o[#o+1] = w(0x010C + i * 2) end
  for k = 0, 7 do
    local b = 0x1000 + k * 0x100
    for _, a in ipairs({0xC8,0xDA,0xE2,0xE6,0x10,0x84,0x90,0x92,0x94,0x96,0xC1}) do
      o[#o+1] = w(b + a)
    end
    local s = w(b + 0xEA); if s > 32767 then s = s - 65536 end
    o[#o+1] = s
    o[#o+1] = w(b + 0x18)
    o[#o+1] = w(b + 0x1C)
  end
  print(table.concat(o, ","))
end)
