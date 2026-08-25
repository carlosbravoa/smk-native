local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local started, n = false, 0
local function s16(v) if v > 32767 then return v - 65536 end return v end
emu.register_frame_done(function()
  local mode = mem:read_u8(0x7E0036)
  if mode == 2 then started = true end
  if not started then return end
  if mode ~= 2 then manager.machine:exit() end
  if n >= 1095 and n <= 1200 and (n % 4 == 0) then
    print(string.format("CAM %d cam94=%d camAC=%04x camA4=%d  A4=%d 2A=%d A2=%d AA=%d A8=%d A6=%d DC=%d",
      n, mem:read_u16(0x7E0094), mem:read_u16(0x7E00AC), mem:read_u16(0x7E00A4), mem:read_u16(0x7E10A4), mem:read_u16(0x7E102A), mem:read_u16(0x7E10A2), s16(mem:read_u16(0x7E10AA)), s16(mem:read_u16(0x7E10A8)), mem:read_u16(0x7E10A6), s16(mem:read_u16(0x7E00DC))))
  end
  n = n + 1
  if n > 1200 then manager.machine:exit() end
end)
