-- Does the ENGINE follow the driver?  Poke the player's character
-- ($1012, doubled: 0 Mario, 2 Luigi, 4 Bowser, 6 Peach, 8 DK, 10 Yoshi,
-- 12 Koopa, 14 Toad) before the race sets its sound up, then report
-- voice 7's sample and pitch.
local cpu = manager.machine.devices[":soundcpu"]
local ram = cpu.spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local who = tonumber(os.getenv("CHAR") or "0")
local at  = tonumber(os.getenv("CHAR_AT") or "1150")
local n = 0
local said = false
emu.register_frame_done(function()
  n = n + 1
  if n >= at and n <= at + 400 then
    mem:write_u8(0x7E1012, who); mem:write_u8(0x7E1013, 0)
  end
  if n == at + 500 and not said then
    said = true
    print(string.format("char %2d -> voice7 SRCN $%02X pitch $%04X  (v5 $%02X, v6 $%02X)",
      who, dsp(0x74), dsp(0x73) * 256 + dsp(0x72), dsp(0x54), dsp(0x64)))
    manager.machine:exit()
  end
end)
