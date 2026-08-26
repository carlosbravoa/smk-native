-- Who writes kart 0's x ($1018) at all, and from where?
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local first, n, printed = {}, 0, false
mem:install_write_tap(0x001018, 0x001019, "gridx", function(off, data, mask)
  n = n + 1
  if #first < 24 and (data & 0xFFFF) > 64 then
    first[#first+1] = string.format("w#%d PC $%02X:%04X = %5d  $0C=$%02X:%04X $12=%d $14=%d",
      n, cpu.state["PB"].value, cpu.state["PC"].value, data & 0xFFFF,
      mem:read_u8(0x7E000E), mem:read_u16(0x7E000C),
      mem:read_u16(0x7E0012), mem:read_u16(0x7E0014))
  end
end)
local frames = 0
emu.register_frame_done(function()
  frames = frames + 1
  if not printed and mem:read_u8(0x7E0036) == 2 then
    printed = true
    print("TOTALWRITES " .. n .. " over " .. frames .. " frames")
    for i = 1, #first do print("  " .. first[i]) end
    print("track $0124 = " .. mem:read_u16(0x7E0124))
    manager.machine:exit()
  end
end)
