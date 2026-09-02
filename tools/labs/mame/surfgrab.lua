-- Record the REAL audio of one surface: music patched off ($81:F504 RTL,
-- nomusic.lua's trick) and the whole course forced to SURF, so whatever
-- is left in the wav is the ground under the kart plus the engine.
-- Used to check a rendered loop against what the chip actually plays.
local rom = manager.machine.memory.regions[":snsslot:cart:rom"]
rom:write_u8(0x01 * 0x10000 + 0xF504, 0x6B)          -- music off
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local CLS  = tonumber(os.getenv("SURF") or "50", 16)
local FROM = tonumber(os.getenv("FROM") or "2800")
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n < FROM then return end
  for a = 0, 255 do mem:write_u8(0x7E0B00 + a, CLS) end
end)
