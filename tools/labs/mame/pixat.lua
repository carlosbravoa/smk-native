-- One frame, numbered exactly as demolog.lua numbers them (counting only
-- while the race is up), so a frame picked out of a demolog CSV is the
-- frame that gets captured.  GVFRAME=n GVOUT=path
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local want = tonumber(os.getenv("GVFRAME") or "0")
local out = os.getenv("GVOUT") or "tmp/frame.ppm"
local logged, started = 0, false
local scr = nil
for tag, s in pairs(manager.machine.screens) do scr = s end
emu.register_frame_done(function()
  local mode = mem:read_u8(0x7E0036)
  if mode == 2 then started = true end
  if not started then return end
  if mode ~= 2 then manager.machine:exit(); return end
  if logged == want then
    local w, h = scr.width, scr.height
    local px = scr:pixels()
    local f = io.open(out, "wb")
    f:write(string.format("P6\n%d %d\n255\n", w, h))
    for i = 0, w * h - 1 do
      local v = string.unpack("<I4", px, i * 4 + 1)
      f:write(string.char((v >> 16) & 255, (v >> 8) & 255, v & 255))
    end
    f:close()
    print(string.format("wrote %s at logged frame %d: E2 %04X A8 %d spd %d surf %02X",
      out, logged, mem:read_u16(0x7E10E2), mem:read_u16(0x7E10A8),
      mem:read_u16(0x7E10EA), mem:read_u8(0x7E10AE)))
    manager.machine:exit()
  end
  logged = logged + 1
end)
