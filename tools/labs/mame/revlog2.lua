-- The in-race engine builder's own numbers ($80:B121): the ceiling
-- $0E20, the deltas $0E22/$0E24/$0E26/$0E28/$0E2A, the rev $C2 and the
-- parameter $42, with speed, the pad and the surface type $B0.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function b(a) return mem:read_u8(0x7E0000 + a) end
local n = 0
print("frame,E20,E22,E24,E26,E28,E2A,C2,p42,speed,pad,B0,target")
emu.register_frame_done(function()
  n = n + 1
  local m = b(0x36) // 2
  if m ~= 6 and m ~= 1 then return end
  print(string.format("%d,%04X,%04X,%04X,%04X,%04X,%04X,%04X,%02X,%d,%04X,%02X,%04X",
    n, w(0x0E20), w(0x0E22), w(0x0E24), w(0x0E26), w(0x0E28), w(0x0E2A),
    w(0x10C2), b(0x42), w(0x10EA), w(0x0020), b(0x10B0), w(0x10D6)))
end)
