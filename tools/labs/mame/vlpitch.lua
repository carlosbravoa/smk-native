-- DSP voice 7 - the engine - per frame against the rev, the note and the
-- kart's state: pitch, volume, echo (NOTES 290).
--   tools/labs/mame/replay.sh vanila-lake-underwater tools/labs/mame/vlpitch.lua 200 > vl.csv
local cpu = manager.machine.devices[":soundcpu"]
local ram = cpu.spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local n = 0
print("f,a0,ac,ea,c2,b0,c4,ae,n42,n43,pitch,voll,volr,srcn,mvoll,evol,eon,flg")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036)
  if m ~= 2 and m ~= 6 then return end
  local function w(a) return mem:read_u16(0x7E0000 + a) end
  print(string.format("%d,%02X,%02X,%d,%d,%04X,%04X,%04X,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", n,
    mem:read_u8(0x7E10A0), mem:read_u8(0x7E10AC), w(0x10EA), w(0x10C2), w(0x10B0), w(0x10C4), w(0x10AE),
    mem:read_u8(0x7E0042), mem:read_u8(0x7E0043),
    dsp(0x73) * 256 + dsp(0x72), dsp(0x70), dsp(0x71), dsp(0x74), dsp(0x0C), dsp(0x2C), dsp(0x4D), dsp(0x6C)))
end)
