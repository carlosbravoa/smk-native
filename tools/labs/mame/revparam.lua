-- The rev machine's live parameters ($0E20..$0E2A, NOTES 143) and the
-- frame counter $38 the tick is gated on, beside $C2 itself.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function s(v) if v > 32767 then return v - 65536 end return v end
local done = false
print("c146,f38,rev,E20,E22,E24,E26,E28,E2A,class")
emu.register_frame_done(function()
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  print(string.format("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
    s(w(0x0146)), mem:read_u8(0x7E0038), s(w(0x10C2)),
    s(w(0x0E20)), s(w(0x0E22)), s(w(0x0E24)), s(w(0x0E26)),
    s(w(0x0E28)), s(w(0x0E2A)), mem:read_u8(0x7E0128)))
end)
