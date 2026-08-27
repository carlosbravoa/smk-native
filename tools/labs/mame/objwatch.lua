-- Do the object slots MOVE on this track?  Log all four every 30 frames.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local n, inrace = 0, false
emu.register_frame_done(function()
  local mode = mem:read_u8(0x7E0036)
  if mode ~= 2 then if inrace then print("race ended") end inrace = false; return end
  if not inrace then inrace = true; n = 0
    print("track $0124 = " .. mem:read_u16(0x7E0124) ..
          "  $0D28 = " .. mem:read_u16(0x7E0D28) ..
          "  $0D2C = " .. mem:read_u16(0x7E0D2C))
  end
  n = n + 1
  if n % 30 == 0 and n <= 1800 then
    local s = {}
    for i = 0, 3 do
      local b = 0x7E1800 + i * 0x80
      s[#s+1] = string.format("(%4d,%4d)", mem:read_u16(b + 0x18), mem:read_u16(b + 0x1C))
    end
    print(string.format("f%-5d %s  wp=%d", n, table.concat(s, " "), mem:read_u8(0x7E10C0)))
  end
end)
