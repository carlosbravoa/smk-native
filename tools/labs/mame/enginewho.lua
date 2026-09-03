-- Which SAMPLE is the engine, and for whom?  Voice 7 carries the
-- player's engine; the drivers' ids sit in the kart blocks.  If each
-- pair of characters has its own engine (the user), the SRCN differs
-- between sessions - and this says which is which.
local cpu = manager.machine.devices[":soundcpu"]
local ram = cpu.spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
local seen = {}
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  if n % 30 ~= 0 then return end
  local drivers = {}
  for q = 0, 7 do drivers[#drivers+1] = string.format("%d", w(0x1000 + q * 0x100 + 0x12)) end
  for v = 0, 7 do
    local srcn = dsp(v * 16 + 4)
    local env = dsp(v * 16 + 8)
    if env > 0 then
      local key = string.format("v%d s%02X", v, srcn)
      if not seen[key] then
        seen[key] = true
        print(string.format("%d,%s,pitch %04X,drivers %s", n, key,
          dsp(v * 16 + 3) * 256 + dsp(v * 16 + 2), table.concat(drivers, " ")))
      end
    end
  end
end)
