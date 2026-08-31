-- What the 65816 is telling the sound driver, every frame, next to the
-- kart's speed: the APU input ports as the SPC700 sees them ($F4-$F7 in
-- its own data space), which is where a continuous ENGINE pitch would
-- have to travel.
local cpu = manager.machine.devices[":soundcpu"]
local dat = cpu.spaces["data"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
print("frame,speed,p0,p1,p2,p3,mode")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  print(string.format("%d,%d,%02X,%02X,%02X,%02X,%d", n, w(0x10EA),
    dat:read_u8(0xF4), dat:read_u8(0xF5), dat:read_u8(0xF6), dat:read_u8(0xF7), m))
end)
