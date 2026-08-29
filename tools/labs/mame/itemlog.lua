-- The item system through a recorded race: the roulette, what it lands
-- on, when it is used, and what the hit reaction looks like on the kart.
--
-- $0D70,y is the item word ($A000 starts the roulette, negative = an item
-- or its roulette is running - NOTES 110); $0D78,y is set to $C1 with it.
-- The whole $0D60-$0DBF window is logged so nothing has to be assumed
-- about which neighbours matter.  Per kart: $10 flags, $AC (crash/knock
-- state), $EE (drive state), $E2, $FA (spin accumulator), speed, coins.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function s(v) if v > 32767 then return v - 65536 end return v end
local n = 0
local h = {"f","c146"}
for i = 0, 47 do h[#h+1] = string.format("d%02X", 0x60 + i*2) end
for k = 0, 7 do
  for _, f in ipairs({"10","AC","EE","E2","FA","spd","coin","x","y","A4"}) do
    h[#h+1] = "k" .. k .. f
  end
end
print(table.concat(h, ","))
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  local o = {n, s(w(0x0146))}
  for i = 0, 47 do o[#o+1] = w(0x0D60 + i*2) end
  for k = 0, 7 do
    local b = 0x1000 + k * 0x100
    o[#o+1] = w(b + 0x10)
    o[#o+1] = mem:read_u8(0x7E0000 + b + 0xAC)
    o[#o+1] = w(b + 0xEE)
    o[#o+1] = w(b + 0xE2)
    o[#o+1] = s(w(b + 0xFA))
    o[#o+1] = s(w(b + 0xEA))
    o[#o+1] = w(0x0E00 + k * 2)
    o[#o+1] = w(b + 0x18)
    o[#o+1] = w(b + 0x1C)
    o[#o+1] = w(b + 0xA4)
  end
  print(table.concat(o, ","))
end)
