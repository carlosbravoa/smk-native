local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local n, shot = 0, 0
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) == 2 then
    shot = shot + 1
    if shot == 900 then
      print(string.format("HDMAEN=%02x  bgmode=%02x", mem:read_u8(0x00420C), mem:read_u8(0x7E00D2)))
      for ch = 0, 7 do
        local r = 0x004300 | (ch << 4)
        print(string.format("ch%d DMAP=%02x B=%02x A=%02x:%04x tbl=%02x:%04x line=%02x",
          ch, mem:read_u8(r), mem:read_u8(r+1), mem:read_u8(r+4), mem:read_u8(r+2) | (mem:read_u8(r+3)<<8),
          mem:read_u8(r+7), mem:read_u8(r+5) | (mem:read_u8(r+6)<<8), mem:read_u8(r+0xA)))
      end
      manager.machine:exit()
    end
  end
  if n > 20000 then manager.machine:exit() end
end)
