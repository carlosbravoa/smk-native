-- The DSP's own voice-7 pitch against the APU byte $0042 and the rev, per
-- frame - the pitch law over the whole range of a race (NOTES 285):
--   tools/labs/mame/replay.sh cc150 tools/labs/mame/pitchlog.lua 130 > pitch.csv
-- per frame: the player's rev $10C2, the byte the APU gets ($0042), and
-- DSP voice 7's pitch and sample, read back through $F2/$F3
local cpu = manager.machine.devices[":soundcpu"]
local ram = cpu.spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local n = 0
print("f,c2,n42,pitch,srcn,ea")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036)
  if m ~= 2 and m ~= 6 then return end
  print(string.format("%d,%d,%d,%d,%d,%d", n, mem:read_u16(0x7E10C2), mem:read_u8(0x7E0042),
    dsp(0x73) * 256 + dsp(0x72), dsp(0x74), mem:read_u16(0x7E10EA)))
end)
