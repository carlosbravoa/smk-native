-- Every object block that comes alive during a recorded race: which
-- block, its first words, and where every kart was - so the AI's own
-- weapons (bank $85 entities) show up as spawns next to an AI kart.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
local prev = {}
local blocks = {}
for b = 0x0800, 0x0FC0, 0x40 do blocks[#blocks+1] = b end
for b = 0x1800, 0x1FC0, 0x40 do blocks[#blocks+1] = b end
for _, b in ipairs(blocks) do prev[b] = "" end
local function state(b)
  local live = (w(b + 0x12) & 0x8000) ~= 0
  local nz = false
  for i = 0, 0x3E, 2 do if w(b + i) ~= 0 then nz = true break end end
  return (live and "L" or "-") .. (nz and "N" or "-")
end
print("f,block,words,karts")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  for _, b in ipairs(blocks) do
    local st = state(b)
    if st ~= prev[b] and ((st:sub(1,1) == "L" and prev[b]:sub(1,1) ~= "L") or (st:sub(2,2) == "N" and prev[b]:sub(2,2) ~= "N")) then
      local o = {}
      for i = 0, 0x3E, 2 do o[#o+1] = string.format("%04X", w(b + i)) end
      local k = {}
      for q = 0, 7 do
        local kb = 0x1000 + q * 0x100
        k[#k+1] = string.format("%d:%d:%d:%04X:%04X", q, w(kb + 0x18), w(kb + 0x1C), w(kb + 0x10), w(kb + 0xE0))
      end
      print(string.format("%d,%04X,%s,%s,%s", n, b, st, table.concat(o, " "), table.concat(k, " ")))
    end
    prev[b] = st
  end
end)
