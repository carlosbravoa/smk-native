-- Every DSP voice's registers, so a sound can be read off the CHIP
-- instead of the speaker: VOL L/R, PITCH ($x2/$x3), SRCN ($x4 - which
-- BRR sample), ADSR/GAIN and ENVX.  ENG_V (hex) pins the engine
-- parameter at $80:9643 first, so the engine's own voice shows up as
-- the one whose PITCH follows it.
local ram = manager.machine.devices[":soundcpu"].spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local v = os.getenv("ENG_V")
if v then
  local rom = manager.machine.memory.regions[":snsslot:cart:rom"]
  rom:write_u8(0x9643, 0xA9); rom:write_u8(0x9644, tonumber(v, 16))
end
local first = tonumber(os.getenv("VOICE_FROM") or "2400")
local every = tonumber(os.getenv("VOICE_EVERY") or "60")
local n = 0
print("frame,voice,voll,volr,pitch,srcn,adsr1,adsr2,gain,envx")
emu.register_frame_done(function()
  n = n + 1
  if n < first or (n - first) % every ~= 0 then return end
  if n > first + every * 12 then manager.machine:exit() end
  for i = 0, 7 do
    local b = i * 16
    print(string.format("%d,%d,%d,%d,%04X,%02X,%02X,%02X,%02X,%d", n, i,
      dsp(b), dsp(b + 1), dsp(b + 3) * 256 + dsp(b + 2), dsp(b + 4),
      dsp(b + 5), dsp(b + 6), dsp(b + 7), dsp(b + 8)))
  end
  print(string.format("%d,DIR,%02X", n, dsp(0x5D)))
end)
