-- Which DSP voices carry the SOUND EFFECTS?
--
-- Each frame, read every voice's ENVX ($x8, the live envelope level) and
-- VOL ($x0/$x1) through the SPC's own $F2/$F3 register window (the way
-- spcdump.lua does), and poke a sound on a schedule.  The voice whose
-- envelope wakes up right after a poke is the SFX voice.
local cpu = manager.machine.devices[":soundcpu"]
local ram = cpu.spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local saved = ram:read_u8(0xF2)
  ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3)
  ram:write_u8(0xF2, saved)
  return v
end
local function w16(a, v)
  mem:write_u8(0x7E0000 + a, v & 0xFF)
  mem:write_u8(0x7E0001 + a, (v >> 8) & 0xFF)
end
local ids = {}
for tok in string.gmatch(os.getenv("SFX_IDS") or "48", "[^,]+") do ids[#ids+1] = tonumber(tok, 16) end
local start = tonumber(os.getenv("SFX_START") or "15000")
local gap = tonumber(os.getenv("SFX_GAP") or "60")
local n = 0
print("frame,event,envx0..7,voll0..7")
emu.register_frame_done(function()
  n = n + 1
  if n < start - 30 then return end
  local ev = ""
  local k = n - start
  if k >= 0 and k % gap == 0 then
    local i = k // gap + 1
    if i <= #ids then
      w16(0x0E6C, ids[i]); w16(0x0E74, 0); w16(0x0E6A, 2)
      ev = string.format("FIRE%04X", ids[i])
    end
  end
  if n > start + #ids * gap + 30 then manager.machine:exit() end
  local e, v = {}, {}
  for i = 0, 7 do
    e[#e+1] = string.format("%3d", dsp(i * 16 + 8))
    v[#v+1] = string.format("%4d", dsp(i * 16))
  end
  print(string.format("%d,%s,%s,%s", n, ev, table.concat(e, " "), table.concat(v, " ")))
end)
