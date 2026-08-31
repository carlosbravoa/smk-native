-- Silence the MUSIC without touching the sound effects: $81:F504 is the
-- entry the game sends a music track through (the boot code's `LDA #$7F
-- / JSL $81F504`, and $81:F4DF's $82); the effects go the other way,
-- through the $0E6C queue and $80:9744.  RTL at $81:F504 means no track
-- is ever started - and then a capture is the effect ALONE, with no
-- subtraction at all.
--
-- ENG_V (hex, optional) also pins the engine parameter at $80:9643.
local rom = manager.machine.memory.regions[":snsslot:cart:rom"]
local function patch(image, bytes, what)
  local o = {}
  for i, b in ipairs(bytes) do
    o[#o+1] = string.format("%02X", rom:read_u8(image + i - 1))
    rom:write_u8(image + i - 1, b)
  end
  print(string.format("%s: %s -> patched at %06X", what, table.concat(o, " "), image))
end
patch(0x01 * 0x10000 + 0xF504, {0x6B}, "music off ($81:F504 RTL)")
local v = os.getenv("ENG_V")
if v then patch(0x00 * 0x10000 + 0x9643, {0xA9, tonumber(v, 16)}, "engine pinned") end
