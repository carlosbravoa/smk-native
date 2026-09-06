-- NOTES 296: kart 1 and the shell through the hit in the shell1 recording (frames 3212-3230):
-- both velocities ($22/$24), the shell type $70, the flags, the exchange at contact
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function s(a) local v = w(a); if v >= 32768 then v = v - 65536 end return v end
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n >= 3212 and n <= 3230 then
    local o, k = 0x1A00, 0x1100
    print(string.format("X f%d obj: 70=%04X 10=%04X 4E=%04X 42=%04X x=%d y=%d z=%d 22=%d 24=%d 2A=%04X EA=%d 26=%d | k1: 22=%d 24=%d 2A=%04X A2=%04X EA=%d", n, w(o+0x70), w(o+0x10), w(o+0x4E), w(o+0x42), w(o+0x18), w(o+0x1C), w(o+0x1F), s(o+0x22), s(o+0x24), w(o+0x2A), w(o+0xEA), s(o+0x26), s(k+0x22), s(k+0x24), w(k+0x2A), w(k+0xA2), w(k+0xEA)))
  end
end)
