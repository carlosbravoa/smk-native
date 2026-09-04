local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n, last = 0, {}
print("f,0FEC,0FE8,0FEA,0FEE,slot0live,slot1live,slot0owner,slot1owner")
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) ~= 2 then return end
  local v = {w(0x0FEC), w(0x0FE8), w(0x0FEA), w(0x0FEE), (w(0x1A12) & 0x8000) ~= 0 and 1 or 0, (w(0x1A92) & 0x8000) ~= 0 and 1 or 0, w(0x1A6A), w(0x1AEA)}
  local key = table.concat(v, ",")
  if key ~= last.key then print(n .. "," .. key); last.key = key end
end)
