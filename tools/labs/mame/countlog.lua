-- The game's own start counter ($0146, $80:9FE1 sets it to -$150) with
-- the voices that are sounding: where does the countdown begin, where
-- does the green light fall, and what is playing in between.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ram = manager.machine.devices[":soundcpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local n = 0
print("frame,count0146,mode,voices")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  local act = {}
  for i = 0, 7 do if dsp(i * 16 + 8) > 0 then act[#act+1] = tostring(i) end end
  print(string.format("%d,%04X,%d,%s", n, w(0x0146), m, table.concat(act, "")))
end)
