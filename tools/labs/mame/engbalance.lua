-- How loud is ANOTHER kart's engine next to your own?  A voice counts as
-- an engine when it carries one of the four engine samples AND sits in
-- the engine pitch range - the sample alone is not enough, because the
-- music plays $03 and $17 as instruments an octave and more below.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local spc = manager.machine.devices[":soundcpu"].spaces["program"]
local function dsp(r)
  local s = spc:read_u8(0xF2); spc:write_u8(0xF2, r)
  local v = spc:read_u8(0xF3); spc:write_u8(0xF2, s); return v
end
local ENG = {[0x02]=true,[0x03]=true,[0x17]=true,[0x18]=true}
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n % 3 ~= 0 then return end
  local out, any = "", 0
  for v = 0, 7 do
    local b = v * 16
    local s, p, e = dsp(b+4), dsp(b+3)*256 + dsp(b+2), dsp(b+8)
    if ENG[s] and e > 0 and p >= 0x4400 and p <= 0x5400 then
      local l, r = dsp(b), dsp(b+1)
      if l > 127 then l = l - 256 end
      if r > 127 then r = r - 256 end
      out = out .. string.format(" %d:s%02X:p%04X:v%d", v, s, p, math.max(math.abs(l), math.abs(r)))
      any = any + 1
    end
  end
  if any > 1 then print("Q " .. n .. out) end
end)
