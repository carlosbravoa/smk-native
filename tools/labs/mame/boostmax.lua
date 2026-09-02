-- How fast does a mushroom actually make you go?
--
-- Poke the item word to a mushroom, force the button (the pad READ at
-- $80:8445 patched for two frames, itemfire.lua's trick), then watch
-- $10EA - the player's speed - and report the peak against $10D6, the
-- class top the boost is supposed to be measured from.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local rom = manager.machine.memory.regions[":snsslot:cart:rom"]
local FIRE = tonumber(os.getenv("FIRE") or "2600")
local COINS = tonumber(os.getenv("COINS") or "-1")
local PAD_READ = 0x8445
local function force(on)
  if on then
    rom:write_u8(PAD_READ, 0xA9); rom:write_u8(PAD_READ+1, 0x80); rom:write_u8(PAD_READ+2, 0x00)
  else
    rom:write_u8(PAD_READ, 0xBD); rom:write_u8(PAD_READ+1, 0x18); rom:write_u8(PAD_READ+2, 0x42)
  end
end
local CHAR = tonumber(os.getenv("CHAR") or "-1")
local function w(a) return mem:read_u8(0x7E0000+a) | (mem:read_u8(0x7E0000+a+1) << 8) end
local n, peak, peak_at = 0, 0, 0
emu.register_frame_done(function()
  n = n + 1
  if CHAR >= 0 and n > 600 then
    mem:write_u8(0x7E1012, CHAR); mem:write_u8(0x7E1013, 0)
  end
  if COINS >= 0 and n > FIRE - 200 and n < FIRE then
    mem:write_u8(0x7E0E00, COINS); mem:write_u8(0x7E0E01, 0)
  end
  if n >= FIRE - 120 and n < FIRE then
    mem:write_u8(0x7E0D70, tonumber(os.getenv('ITEM') or '0', 16))
    mem:write_u8(0x7E0D71, 0xC0)   -- $C000|id, ready
  end
  if n == FIRE - 1 then force(true) end
  if n == FIRE + 1 then force(false) end
  if n >= FIRE and n <= FIRE + 240 then
    local sp = w(0x10EA)
    if sp > peak then peak = sp; peak_at = n end
    if false then
      print(string.format("  f%d speed %4d  target $D6 %4d  state $A6 %04X",
        n, sp, w(0x10D6), w(0x10A6)))
    end
  end
  if n == FIRE + 240 then
    print(string.format("PEAK %d at f%d (%+d over the class top %d), coins %d",
      peak, peak_at, peak - w(0x10D6), w(0x10D6), w(0x0E00)))
    manager.machine:exit()
  end
end)
