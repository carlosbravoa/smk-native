-- The envelope and pitch of ONE surface's held voice, every frame, with
-- the whole course forced to that class ($0B00, NOTES 011).
-- SURF=<class> SAMPLE=<srcn>
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local spc = manager.machine.devices[":soundcpu"].spaces["program"]
local function dsp(r)
  local s = spc:read_u8(0xF2); spc:write_u8(0xF2, r)
  local v = spc:read_u8(0xF3); spc:write_u8(0xF2, s); return v
end
local CLS = tonumber(os.getenv("SURF") or "5A", 16)
local SMP = tonumber(os.getenv("SAMPLE") or "04", 16)
local FROM = 2800
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n < FROM then return end
  if n > FROM + 500 then manager.machine:exit(); return end
  for a = 0, 255 do mem:write_u8(0x7E0B00 + a, CLS) end
  if n > FROM + 60 then
    for v = 0, 7 do
      if dsp(v*16+4) == SMP then
        print(string.format("E %d %d %d %04X", n, v, dsp(v*16+8),
          dsp(v*16+3) * 256 + dsp(v*16+2)))
        break
      end
    end
  end
end)
