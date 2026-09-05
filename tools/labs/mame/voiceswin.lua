-- every DSP voice per frame inside the windows around the tumbles, plus
-- the APU bytes $42/$43 and the player's and karts' spin states
local cpu = manager.machine.devices[":soundcpu"]
local ram = cpu.spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local WIN = { {2600, 2760}, {8330, 8490}, {13180, 13340}, {20140, 20300}, {24040, 24200} }
local n = 0
print("f,n42,n43,pa6,ai7a6,ai2a6,kon,v0,v1,v2,v3,v4,v5,v6,v7")
emu.register_frame_done(function()
  n = n + 1
  local inwin = false
  for _, w in ipairs(WIN) do if n >= w[1] and n <= w[2] then inwin = true end end
  if not inwin then return end
  local parts = {}
  for v = 0, 7 do
    local b = v * 16
    parts[#parts+1] = string.format("%02X:%d:%d:%d", dsp(b + 4), dsp(b + 3) * 256 + dsp(b + 2), dsp(b + 0), dsp(b + 8))
  end
  print(string.format("%d,%d,%d,%02X,%02X,%02X,%02X,%s", n, mem:read_u8(0x7E0042), mem:read_u8(0x7E0043),
    mem:read_u8(0x7E10A6), mem:read_u8(0x7E17A6), mem:read_u8(0x7E12A6), dsp(0x4C), table.concat(parts, ",")))
end)
