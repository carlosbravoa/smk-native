-- one AI kart's motion through a shell hit, per frame: position, heading,
-- velocity, speed, states (NOTES 292)
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function sw(a) local v = w(a); if v >= 32768 then v = v - 65536 end return v end
local n = 0
local WIN = { {3310, 3380, 5}, {4155, 4225, 7}, {4670, 4740, 6} }
print("f,k,x,xf,y,yf,a4,vx,vy,ea,a6,ac,e4,aa,a8")
emu.register_frame_done(function()
  n = n + 1
  for _, wn in ipairs(WIN) do
    if n >= wn[1] and n <= wn[2] then
      local b = 0x1000 + wn[3] * 0x100
      print(string.format("%d,%d,%d,%d,%d,%d,%04X,%d,%d,%d,%02X,%02X,%d,%d,%d", n, wn[3], w(b + 0x18), mem:read_u8(0x7E0000 + b + 0x17), w(b + 0x1C), mem:read_u8(0x7E0000 + b + 0x1B),
        w(b + 0xA4), sw(b + 0x14), sw(b + 0x16), w(b + 0xEA), mem:read_u8(0x7E0000 + b + 0xA6), mem:read_u8(0x7E0000 + b + 0xAC), sw(b + 0xE4), sw(b + 0xAA), sw(b + 0xA8)))
    end
  end
end)
