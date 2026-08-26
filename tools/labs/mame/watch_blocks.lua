-- Watch a recorded session for the moment a block disappears: report any
-- change in the live tilemap ($7F:0000-$7F:3FFF) with the kart's position
-- and state, plus the class table and the object blocks.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local snap, n, races = nil, 0, 0
local function s16(v) if v > 32767 then return v - 65536 end return v end
emu.register_frame_done(function()
  n = n + 1
  local mode = mem:read_u8(0x7E0036)
  if mode ~= 2 and mode ~= 1 and mode ~= 6 then return end
  local cur = {}
  for i = 0, 0x3FFF do cur[i] = mem:read_u8(0x7F0000 + i) end
  if snap then
    for i = 0, 0x3FFF do
      if cur[i] ~= snap[i] then
        print(string.format("f%d cell %d (%d,%d) tile %02X -> %02X class %02X -> %02X | kart (%d,%d) spd %d $10=%04X AE=%02X A0=%d AC=%d",
          n, i, (i % 128) * 8, (i // 128) * 8, snap[i], cur[i],
          mem:read_u8(0x7E0B00 + snap[i]), mem:read_u8(0x7E0B00 + cur[i]),
          mem:read_u16(0x7E1018), mem:read_u16(0x7E101C), s16(mem:read_u16(0x7E10EA)),
          mem:read_u16(0x7E1010), mem:read_u8(0x7E10AE), mem:read_u16(0x7E10A0), mem:read_u16(0x7E10AC)))
      end
    end
  end
  snap = cur
end)
