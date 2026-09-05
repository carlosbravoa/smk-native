-- The DSP through the player's spin, every register (NOTES 293): the spin's
-- own voice is sample $00 keyed ten frames in, pitch walked on a triangle.
--   tools/labs/mame/replay.sh spin1 tools/labs/mame/voicesnoise.lua 90 > voices.csv (edit the frame window)
-- the DSP through the player's tumble, with the global registers: noise
-- enable NON, pitch modulation PMON, echo EON, FLG (noise clock), and per
-- voice SRCN:PITCH:VOL:ENVX:ADSR1
local cpu = manager.machine.devices[":soundcpu"]
local ram = cpu.spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local n = 0
print("f,pa6,n42,n43,non,pmon,eon,flg,kof,v0,v1,v2,v3,v4,v5,v6,v7")
emu.register_frame_done(function()
  n = n + 1
  if n < 20120 or n > 20260 then return end
  local parts = {}
  for v = 0, 7 do
    local b = v * 16
    parts[#parts+1] = string.format("%02X:%d:%d:%d:%02X", dsp(b + 4), dsp(b + 3) * 256 + dsp(b + 2), dsp(b + 0), dsp(b + 8), dsp(b + 5))
  end
  print(string.format("%d,%02X,%d,%d,%02X,%02X,%02X,%02X,%02X,%s", n, mem:read_u8(0x7E10A6), mem:read_u8(0x7E0042), mem:read_u8(0x7E0043),
    dsp(0x3D), dsp(0x2D), dsp(0x4D), dsp(0x6C), dsp(0x5C), table.concat(parts, ",")))
end)
