-- one projectile block through a window: position, velocity, height, flags
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function s(v) if v > 32767 then return v - 65536 end return v end
local blk, f0, f1 = tonumber(os.getenv("BLK")), tonumber(os.getenv("F0")), tonumber(os.getenv("F1"))
local n = 0
print("f,live,owner,var,handler6E,6C,x,y,z,vx,vy,f42,f10,ownerx,ownery,px,py,dist_to_player,spd72")
emu.register_frame_done(function()
  n = n + 1
  if n < f0 or n > f1 then if n > f1 then manager.machine:exit() end return end
  local o = w(blk + 0x6A)
  local ox, oy = 0, 0
  if o >= 0x1000 and o <= 0x1700 then ox, oy = w(o + 0x18), w(o + 0x1C) end
  local x, y = w(blk + 0x18), w(blk + 0x1C)
  local dx, dy = w(0x1018) - x, w(0x101C) - y
  print(string.format("%d,%d,$%04X,%d,$%04X,%d,%d,%d,%d,%d,%d,$%04X,$%04X,%d,%d,%d,%d,%d,%d", n, (w(blk+0x12) & 0x8000) ~= 0 and 1 or 0, o, w(blk+0x70), w(blk+0x6E), w(blk+0x6C),
    x, y, w(blk+0x1E) | (mem:read_u8(0x7E0000+blk+0x20) << 16), s(w(blk+0x22)), s(w(blk+0x24)), w(blk+0x42), w(blk+0x10), ox, oy, w(0x1018), w(0x101C), math.floor(math.sqrt(dx*dx+dy*dy)), s(w(blk+0x72))))
end)
