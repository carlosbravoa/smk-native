-- NOTES 295: kart 4 through its item hop in the aihop recording (frames 2746-2792):
-- z, zvel, heading, pose, states, and the sound queue - the arc and "no roll, no sound"
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n >= 2746 and n <= 2792 then
    local b = 0x1400
    print(string.format("P f%d z=%d zv=%d A4=%04X AA=%04X A6=%02X A0=%02X E2=%04X q=%02X%02X%02X%02X %02X%02X", n, w(b+0x1F), w(b+0x26), w(b+0xA4), w(b+0xAA), w(b+0xA6), w(b+0xA0), w(b+0xE2), mem:read_u8(0x7E0E6A), mem:read_u8(0x7E0E6B), mem:read_u8(0x7E0E6C), mem:read_u8(0x7E0E6D), mem:read_u8(0x7E0042), mem:read_u8(0x7E0043)))
  end
end)
