-- How does another kart's engine volume fall off with DISTANCE?
--
-- The player's engine is voice 7.  A voice counts as another kart's
-- engine when it carries one of the four engine samples AND sits in the
-- engine pitch band $4400..$5400 - the music plays two of those samples
-- an octave down, so the sample alone counts the wrong voices.  Logged
-- beside every kart's position, so volume can be put against distance.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local spc = manager.machine.devices[":soundcpu"].spaces["program"]
local function dsp(r)
  local s = spc:read_u8(0xF2); spc:write_u8(0xF2, r)
  local v = spc:read_u8(0xF3); spc:write_u8(0xF2, s); return v
end
local ENG = {[0x02]=true,[0x03]=true,[0x17]=true,[0x18]=true}
local function w(a) return mem:read_u8(0x7E0000+a) | (mem:read_u8(0x7E0000+a+1) << 8) end
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n % 4 ~= 0 then return end
  local out, any = "", false
  for v = 0, 7 do
    local b = v * 16
    local s, p, e = dsp(b+4), dsp(b+3)*256 + dsp(b+2), dsp(b+8)
    if ENG[s] and e > 0 and p >= 0x4400 and p <= 0x5400 then
      local l, r = dsp(b), dsp(b+1)
      if l > 127 then l = l - 256 end
      if r > 127 then r = r - 256 end
      out = out .. string.format(" %d:%02X:%04X:%d", v, s, p, math.max(math.abs(l), math.abs(r)))
      any = true
    end
  end
  if not any then return end
  local pos = ""
  for q = 0, 7 do
    pos = pos .. string.format(" %d,%d", w(0x1000 + q*0x100 + 0x18), w(0x1000 + q*0x100 + 0x1C))
  end
  print("D " .. n .. " V" .. out .. " K" .. pos)
end)
