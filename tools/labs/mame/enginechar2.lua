-- The engine, per driver, with the REV PINNED so the pitches compare.
-- Patches $80:9643 (LDA $42 -> LDA #v) and holds the player's character
-- at $1012 every frame, then reports what voice 7 is doing - and reads
-- the character back, so a poke the game overwrites cannot look like a
-- result.
local rom = manager.machine.memory.regions[":snsslot:cart:rom"]
rom:write_u8(0x9643, 0xA9); rom:write_u8(0x9644, tonumber(os.getenv('ENG_V') or '30', 16))
local cpu = manager.machine.devices[":soundcpu"]
local ram = cpu.spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local who = tonumber(os.getenv("CHAR") or "0")
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n > 600 then
    mem:write_u8(0x7E1012, who); mem:write_u8(0x7E1013, 0)
  end
  if n == 2500 then
    print(string.format("char %2d (reads back %2d) -> v7 SRCN $%02X pitch $%04X env %d",
      who, mem:read_u8(0x7E1012), dsp(0x74), dsp(0x73) * 256 + dsp(0x72), dsp(0x78)))
    manager.machine:exit()
  end
end)
