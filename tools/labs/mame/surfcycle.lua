-- Which surface classes make the off-road hiss?  One run: force $0B00 to
-- a different class every 200 frames and record whether DSP voice 5 has
-- sample $04 keyed under each.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local spc = manager.machine.devices[":soundcpu"].spaces["program"]
local function dsp(r)
  local s = spc:read_u8(0xF2); spc:write_u8(0xF2, r)
  local v = spc:read_u8(0xF3); spc:write_u8(0xF2, s); return v
end
local CLASSES = {0x40, 0x42, 0x44, 0x4A, 0x50, 0x52, 0x54, 0x58,
                 0x5A, 0x5C, 0x5E, 0x22, 0x1A, 0x10, 0x40}
local FROM, SPAN = 2800, 200
local n, hits, rows = 0, {}, {}
for i = 1, #CLASSES do hits[i] = 0; rows[i] = 0 end
emu.register_frame_done(function()
  n = n + 1
  if n < FROM then return end
  local i = math.floor((n - FROM) / SPAN) + 1
  if i > #CLASSES then
    for k, c in ipairs(CLASSES) do
      print(string.format("C $%02X: sample $04 keyed on %d of %d frames", c, hits[k], rows[k]))
    end
    manager.machine:exit(); return
  end
  for a = 0, 255 do mem:write_u8(0x7E0B00 + a, CLASSES[i]) end
  if (n - FROM) % SPAN > 40 then           -- let the state settle
    rows[i] = rows[i] + 1
    for v = 0, 7 do
      if dsp(v * 16 + 4) == 0x04 and dsp(v * 16 + 8) > 0 then
        hits[i] = hits[i] + 1; break
      end
    end
  end
end)
