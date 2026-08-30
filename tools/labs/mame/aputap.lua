-- Every write the 65816 makes to the APU ports ($2140-$2143, bank 0 and
-- bank $80 mirrors), with the frame - the driver's song-select protocol
-- read off a replay's transitions.  The tap objects must stay referenced.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local n = 0
emu.register_frame_done(function() n = n + 1 end)
local last = {}
local function hook(offset, data, mask)
  local port = offset & 3
  local v = data & 0xFF
  if last[port] ~= v then
    print(string.format("f%d port%d = %02X", n, port, v))
    last[port] = v
  end
end
_G.tap0 = mem:install_write_tap(0x002140, 0x002143, "aputap0", hook)
_G.tap8 = mem:install_write_tap(0x802140, 0x802143, "aputap8", hook)
