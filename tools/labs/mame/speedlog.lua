-- P1's speed every frame (and an optional sound poke at SFX_START), so a
-- recorded capture's audio can be correlated with how fast the kart was.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function w16(a, v)
  mem:write_u8(0x7E0000 + a, v & 0xFF); mem:write_u8(0x7E0001 + a, (v >> 8) & 0xFF)
end
local start = tonumber(os.getenv("SFX_START") or "0")
local id = tonumber(os.getenv("SFX_ID") or "0", 16)
local n = 0
print("frame,speed,mode")
emu.register_frame_done(function()
  n = n + 1
  if start > 0 and n == start and id > 0 then
    w16(0x0E6C, id); w16(0x0E74, 0); w16(0x0E6A, 2)
    print(string.format("FIRE %d %04X", n, id))
  end
  if n % 6 == 0 then
    print(string.format("%d,%d,%d", n, w(0x10EA), mem:read_u8(0x7E0036) // 2))
  end
end)
