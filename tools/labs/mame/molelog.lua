-- the moles recording: every race frame - the ride ($1050/$1052/$105E),
-- speed, pad, the sound queue, the four entity blocks' +$1E/+$20/+$00, and
-- the OAM while a mole rides or every 4th frame otherwise
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function r8(a) return mem:read_u8(0x7E0000 + a) end
local function r16(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
print("f,mode,track,x,y,spd,pad,a0,ac,r50,r52,r5e,q6a,queue,ents,oam")
emu.register_frame_done(function()
  n = n + 1
  local m = r8(0x36)
  if m ~= 2 and m ~= 6 and m ~= 12 then return end
  local riding = r16(0x1050) ~= 0
  local q = {}
  for i = 0, 0x13 do q[#q+1] = string.format("%02X", r8(0x0E6C + i)) end
  local e = {}
  for _, base in ipairs({0x1800, 0x1840, 0x1880, 0x18C0}) do
    e[#e+1] = string.format("%04X:%04X:%04X:%04X:%04X", r16(base), r16(base + 0x1E), r16(base + 0x20), r16(base + 0x04), r16(base + 0x08))
  end
  local o = ""
  if riding or n % 4 == 0 then
    local t = {}
    for i = 0, 511 do t[#t+1] = string.format("%02X", r8(0x0400 + i)) end
    o = table.concat(t)
  end
  print(string.format("%d,%02X,%d,%d,%d,%d,%04X,%d,%d,%04X,%04X,%04X,%d,%s,%s,%s", n, m, r16(0x0124), r16(0x1018), r16(0x101C), r16(0x10EA), r16(0x0018), r8(0x10A0), r8(0x10AC), r16(0x1050), r16(0x1052), r16(0x105E), r8(0x0E6A), table.concat(q), table.concat(e, "|"), o))
end)
