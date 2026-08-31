-- P1's whole kart block ($1000-$10FE), changed words only, in the frame
-- windows around the mole touches - where does "mole on face" live?
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
local prev = {}
emu.register_frame_done(function()
  n = n + 1
  if not ((n >= 15250 and n <= 15650) or (n >= 17480 and n <= 17700)) then return end
  local out = {}
  for o = 0, 0xFE, 2 do
    local v = w(0x1000 + o)
    if prev[o] ~= v then
      if prev[o] ~= nil then out[#out+1] = string.format("+%02X=%04X", o, v) end
      prev[o] = v
    end
  end
  if #out > 0 then print(string.format("%d %s", n, table.concat(out, " "))) end
end)
