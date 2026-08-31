-- The engine parameter and what drives it: the pad word the rev law
-- reads ($0020), $42 itself, and the rev accumulator $C2/$C4 in the
-- player's kart block - so the port's transcription can be replayed
-- against the game's own numbers.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function b(a) return mem:read_u8(0x7E0000 + a) end
local n = 0
print("frame,pad,p42,c2,c4,speed,mode")
emu.register_frame_done(function()
  n = n + 1
  local m = b(0x36) // 2
  if m ~= 6 and m ~= 1 then return end
  print(string.format("%d,%04X,%02X,%04X,%04X,%d,%d",
    n, w(0x0020), b(0x42), w(0x10C2), w(0x10C4), w(0x10EA), m))
end)
