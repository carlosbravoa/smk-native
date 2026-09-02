-- What does the game IGNORE during the countdown?
--
-- The pad READ at $80:8445 is patched to a fixed word for a window of
-- frames (the same trick as itemfire.lua), and the kart's heading, Z and
-- speed are logged either side.  Run once with PAD=0000 as the control
-- and once with LEFT ($0200) or HOP ($0020) and the difference is what
-- the game let through.
--
--   PAD=0200 FROM=<frame> TO=<frame>
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local rom = manager.machine.memory.regions[":snsslot:cart:rom"]
local PAD  = tonumber(os.getenv("PAD") or "0000", 16)
local FROM = tonumber(os.getenv("FROM") or "1000")
local TO   = tonumber(os.getenv("TO") or "1200")
local PAD_READ = 0x8445
local function force(on)
  if on then
    rom:write_u8(PAD_READ, 0xA9)
    rom:write_u8(PAD_READ+1, PAD & 0xFF); rom:write_u8(PAD_READ+2, (PAD >> 8) & 0xFF)
  else
    rom:write_u8(PAD_READ, 0xBD); rom:write_u8(PAD_READ+1, 0x18); rom:write_u8(PAD_READ+2, 0x42)
  end
end
local function w(a) return mem:read_u8(0x7E0000+a) | (mem:read_u8(0x7E0000+a+1) << 8) end
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if PAD ~= 0 then
    if n == FROM - 1 then force(true) end
    if n == TO then force(false) end
  end
  if n >= FROM - 2 and n <= TO + 40 and n % 4 == 0 then
    -- $B4 heading, $1F Z, $EA speed, $A4 the drive angle, $10 flags
    -- $B4 heading, $B2 turn (the LEAN), $2A pose, $AA pose lag, $1F Z
    print(string.format("K %d head %04X turn %04X pose %04X aa %04X z %04X spd %d",
      n, w(0x10B4), w(0x10B2), w(0x102A), w(0x10AA), w(0x101F), w(0x10EA)))
  end
  if n > TO + 60 then manager.machine:exit() end
end)
