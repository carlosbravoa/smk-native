local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local n, shot = 0, 0
local scr = nil
for tag, s in pairs(manager.machine.screens) do scr = s end
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) == 2 then
    shot = shot + 1
    if shot == 900 then
      local w, h = scr.width, scr.height
      local px = scr:pixels()
      local f = io.open(os.getenv("PIXOUT") or "frame.ppm", "wb")
      f:write(string.format("P6\n%d %d\n255\n", w, h))
      for y = 0, h - 1 do
        for x = 0, w - 1 do
          local i = (y * w + x) * 4 + 1
          local b, g, r = px:byte(i), px:byte(i+1), px:byte(i+2)
          f:write(string.char(r or 0, g or 0, b or 0))
        end
      end
      f:close()
      print(string.format("PIX %dx%d track %d", w, h, mem:read_u16(0x7E0124)))
      manager.machine:exit()
    end
  end
  if n > 20000 then manager.machine:exit() end
end)
