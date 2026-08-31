-- The $1800 entity blocks, per frame: position and the candidate motion
-- fields, plus P1's own position/state - the fish's jump, the plant's
-- touch, the mole's pop, straight from a recorded drive.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
local prev = ""
print("f;P1x,P1y,A6,E2;blocks base:x,y,+06,+1E,+20,+26,+2A")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  local o = {}
  for b = 0x1800, 0x19C0, 0x40 do
    if w(b) ~= 0 then
      o[#o+1] = string.format("%04X:%d,%d,%04X,%04X,%04X,%04X,%04X",
        b, w(b+0x18), w(b+0x1C), w(b+0x06), w(b+0x1E), w(b+0x20), w(b+0x26), w(b+0x2A))
    end
  end
  local line = table.concat(o, " ")
  if line ~= prev then
    print(string.format("%d;%d,%d,%02X,%04X;%s", n,
      w(0x1018), w(0x101C), mem:read_u8(0x7E10A6), w(0x10E2), line))
    prev = line
  end
end)
