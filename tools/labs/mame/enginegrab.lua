-- The ENGINE, isolated.  $80:9643 is `LDA $42 / STA $2142`: $42 is the
-- engine parameter the 65816 hands the driver every frame (it tracks
-- speed - NOTES 212).  Patching the LDA to an IMMEDIATE pins the engine
-- at one pitch for the whole run, so two runs at two constants differ by
-- the engine alone and nothing else - the music is untouched.
local regions = manager.machine.memory.regions
local rom = regions[":snsslot:cart:rom"]
if not rom then
  for k, _ in pairs(regions) do print("region: " .. k) end
  error("no cart region")
end
local v = tonumber(os.getenv("ENG_V") or "30", 16)
-- $80:9643 is bank $80 offset $9643; HiROM image offset = bank*0x10000 + addr
local base = 0x00 * 0x10000 + 0x9643
print(string.format("patching %02X %02X -> A9 %02X at image %06X",
  rom:read_u8(base), rom:read_u8(base + 1), v, base))
rom:write_u8(base, 0xA9)          -- LDA #imm
rom:write_u8(base + 1, v)
