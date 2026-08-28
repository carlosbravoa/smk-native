-- What a whole recorded race holds.  P1 and the field: position, speed,
-- coins, rank, hazard and the lap word, per frame - so one recording can
-- answer the AI, the coin rules, the impacts and the finish at once.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function s(v) if v > 32767 then return v - 65536 end return v end
local n = 0
print("f,mode,c146,lapword,px,py,pspd,pcoin,prank,pA0,pC8," ..
      "k1spd,k1rank,k1C8,k2spd,k2rank,k2C8,k3spd,k3rank,k3C8")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  local o = {}
  for _, k in ipairs({1, 2, 3}) do
    local b = 0x1000 + k * 0x100
    o[#o+1] = string.format("%d,%d,%d", s(w(b+0xEA)), w(b+0xE6)//2, w(b+0xC8) & 0xFF)
  end
  print(string.format("%d,%d,%d,%04X,%d,%d,%d,%d,%d,%04X,%d,%s",
    n, m, s(w(0x0146)), w(0x10C0), w(0x1018), w(0x101C), s(w(0x10EA)),
    w(0x0E00), w(0x10E6)//2, w(0x10A0), w(0x10C8) & 0xFF,
    table.concat(o, ",")))
end)
