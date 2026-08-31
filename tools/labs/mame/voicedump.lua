-- Every DSP voice, EVERY frame, around a poked sound: this is how an
-- effect is captured now (NOTES 213).  Reading the chip instead of the
-- speaker means the music cannot bleed in - the renderer keeps only the
-- voices that differ from a baseline run and rebuilds them from the
-- game's own BRR samples.
--
--   SFX_ID (hex, optional)  the sound to poke at SFX_START
--   SFX_START               the frame to poke on
local ram = manager.machine.devices[":soundcpu"].spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local function w16(a, v)
  mem:write_u8(0x7E0000 + a, v & 0xFF); mem:write_u8(0x7E0001 + a, (v >> 8) & 0xFF)
end
local id = os.getenv("SFX_ID")
local start = tonumber(os.getenv("SFX_START") or "2200")
local after = tonumber(os.getenv("SFX_AFTER") or "150")
local n = 0
print("frame,voice,voll,volr,pitch,srcn,envx")
emu.register_frame_done(function()
  n = n + 1
  if id and n == start then
    w16(0x0E6C, tonumber(id, 16)); w16(0x0E74, 0); w16(0x0E6A, 2)
    print("FIRE " .. n .. " " .. id)
  end
  if n < start - 4 then return end
  if n > start + after then manager.machine:exit() end
  for i = 0, 7 do
    local b = i * 16
    local vl, vr = dsp(b), dsp(b + 1)
    if vl > 127 then vl = vl - 256 end
    if vr > 127 then vr = vr - 256 end
    print(string.format("%d,%d,%d,%d,%04X,%02X,%d", n, i, vl, vr,
      dsp(b + 3) * 256 + dsp(b + 2), dsp(b + 4), dsp(b + 8)))
  end
end)
