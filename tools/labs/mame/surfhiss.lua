-- One class per run, all from the same frame, so the kart meets each
-- surface in the SAME state: how much of the window has DSP sample $04
-- keyed (the off-road hiss), and how fast the kart is while it does.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local spc = manager.machine.devices[":soundcpu"].spaces["program"]
local function dsp(r)
  local s = spc:read_u8(0xF2); spc:write_u8(0xF2, r)
  local v = spc:read_u8(0xF3); spc:write_u8(0xF2, s); return v
end
local CLS  = tonumber(os.getenv("SURF") or "40", 16)
local FROM = tonumber(os.getenv("FROM") or "2800")
local SPAN = tonumber(os.getenv("SPAN") or "600")
local n, hits, rows, spd, pitch = 0, 0, 0, 0, {}
emu.register_frame_done(function()
  n = n + 1
  if n < FROM then return end
  if n > FROM + SPAN then
    local ps = {}
    for p in pairs(pitch) do ps[#ps+1] = string.format("$%04X", p) end
    print(string.format("H $%02X: $04 keyed %d/%d frames, mean speed %d, pitches %s",
      CLS, hits, rows, rows > 0 and spd // rows or 0, table.concat(ps, ",")))
    manager.machine:exit(); return
  end
  for a = 0, 255 do mem:write_u8(0x7E0B00 + a, CLS) end
  if n > FROM + 60 then
    rows = rows + 1
    spd = spd + (mem:read_u8(0x7E10EA) | (mem:read_u8(0x7E10EB) << 8))
    for v = 0, 7 do
      if dsp(v * 16 + 4) == 0x04 and dsp(v * 16 + 8) > 0 then
        hits = hits + 1
        pitch[dsp(v * 16 + 3) * 256 + dsp(v * 16 + 2)] = true
        break
      end
    end
  end
end)
