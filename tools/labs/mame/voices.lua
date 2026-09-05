-- Every DSP voice per frame - sample, pitch, volume - and the player's speed
-- and rev (NOTES 291): which voices carry the OTHER karts' engines, and at
-- what pitch.
--   tools/labs/mame/replay.sh cc150 tools/labs/mame/voices.lua 75 > voices.csv
-- every DSP voice's pitch, volume and sample per frame (NOTES 291)
local cpu = manager.machine.devices[":soundcpu"]
local ram = cpu.spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local n = 0
print("f,ea,c2,v0,v1,v2,v3,v4,v5,v6,v7")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036)
  if m ~= 2 and m ~= 6 then return end
  if n % 4 ~= 0 then return end
  local parts = {}
  for v = 0, 7 do
    local b = v * 16
    parts[#parts+1] = string.format("%02X:%d:%d", dsp(b + 4), dsp(b + 3) * 256 + dsp(b + 2), dsp(b + 0))
  end
  print(string.format("%d,%d,%d,%s", n, mem:read_u16(0x7E10EA), mem:read_u16(0x7E10C2), table.concat(parts, ",")))
end)
