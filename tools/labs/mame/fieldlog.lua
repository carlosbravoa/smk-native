-- The whole field through a recorded race: who each kart is, its rank,
-- its rubber-band row, its speed, and how far it is from the player.
-- For "DK Jr was always right behind me" (NOTES 171).
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function s(v) if v > 32767 then return v - 65536 end return v end
local n = 0
local hdr = false
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  if not hdr then
    print("f,class,pcoin,px,py,pspd," ..
          "k0ch,k0rk,k0row,k0spd,k0d,k1ch,k1rk,k1row,k1spd,k1d," ..
          "k2ch,k2rk,k2row,k2spd,k2d,k3ch,k3rk,k3row,k3spd,k3d," ..
          "k4ch,k4rk,k4row,k4spd,k4d,k5ch,k5rk,k5row,k5spd,k5d," ..
          "k6ch,k6rk,k6row,k6spd,k6d,k7ch,k7rk,k7row,k7spd,k7d")
    hdr = true
  end
  local px, py = w(0x1018), w(0x101C)
  local o = {}
  for k = 0, 7 do
    local b = 0x1000 + k * 0x100
    local dx, dy = w(b+0x18) - px, w(b+0x1C) - py
    local d = math.floor(math.sqrt(dx*dx + dy*dy))
    o[#o+1] = string.format("%d,%d,%d,%d,%d", w(b+0x12)//2, w(b+0xE6)//2,
                            w(b+0xC8) & 0xFF, s(w(b+0xEA)), d)
  end
  print(string.format("%d,%d,%d,%d,%d,%d,%s", n, mem:read_u8(0x7E0128),
        w(0x0E00), px, py, s(w(0x10EA)), table.concat(o, ",")))
end)
