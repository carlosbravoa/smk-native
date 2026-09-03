-- Is the slide a HELD voice rather than a queued sound?
--
-- NOTES 265 found no sound queued while sliding, on either track.  A held
-- voice would never go through the queue - the engine does not either -
-- so this reads the DSP itself: every voice's SRCN (which BRR sample) and
-- PITCH, on the frames the drift bits are set, plus the class under the
-- kart.  Run it on two recordings and the answer to "same sample, higher
-- pitch?" is a diff.
local ram = manager.machine.devices[":soundcpu"].spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local n = 0
local seen = {}
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) ~= 2 then return end
  local e2 = mem:read_u16(0x7E10E2)
  local drift = (e2 & 0x0004) ~= 0
  local surf = mem:read_u8(0x7E10AE)
  local spd = mem:read_u16(0x7E10EA)
  local t = {}
  for v = 0, 7 do
    local b = v * 16
    local srcn = dsp(b + 4)
    local pitch = dsp(b + 2) | (dsp(b + 3) << 8)
    local envx = dsp(b + 8)
    if envx > 0 then
      t[#t+1] = string.format("v%d:s%02X p%04X e%02X", v, srcn, pitch, envx)
    end
  end
  print(string.format("%s %d surf %02X spd %4d E2 %04X | %s",
    drift and "D" or ".", n, surf, spd, e2, table.concat(t, " ")))
end)
