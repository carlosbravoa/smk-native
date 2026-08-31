-- Trigger sounds on a schedule during a recorded race so they can be
-- recorded with -wavwrite and cut apart offline.
--
-- The game's own request path ($81:F5E2): the id goes to $0E6C,x with
-- the index in $0E6A - so poking the queue asks the real driver for the
-- real sound, in the real race state.  SFX_IDS / SFX_START / SFX_GAP
-- come from the environment; SFX_SILENT=1 runs the same schedule WITHOUT
-- poking, for the baseline that gets subtracted away.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w16(a, v)
  mem:write_u8(0x7E0000 + a, v & 0xFF)
  mem:write_u8(0x7E0001 + a, (v >> 8) & 0xFF)
end
local ids = {}
for tok in string.gmatch(os.getenv("SFX_IDS") or "", "[^,]+") do
  ids[#ids + 1] = tonumber(tok, 16)
end
local start = tonumber(os.getenv("SFX_START") or "16000")
local gap   = tonumber(os.getenv("SFX_GAP") or "90")
local silent = os.getenv("SFX_SILENT") ~= nil
-- SFX_QUIET: poke this id QUIET_LEAD frames before the first sound.  Id
-- $17 stops the music dead (measured: everything after it is a single
-- steady tone), which makes the baseline subtraction exact instead of
-- fighting a diverging song.
local quiet = tonumber(os.getenv("SFX_QUIET") or "0", 16)
local qlead = tonumber(os.getenv("SFX_QUIET_LEAD") or "180")
local n = 0
for i, id in ipairs(ids) do
  print(string.format("plan %d %04X", start + (i - 1) * gap, id))
end
emu.register_frame_done(function()
  n = n + 1
  if quiet > 0 and n == start - qlead then
    w16(0x0E6C, quiet); w16(0x0E74, 0); w16(0x0E6A, 2)
    print(string.format("quiet %d %04X", n, quiet))
  end
  if n < start then return end
  local k = (n - start)
  if k % gap ~= 0 then return end
  local i = k // gap + 1
  if i > #ids then return end
  if not silent then
    w16(0x0E6C, ids[i])
    w16(0x0E74, 0)
    w16(0x0E6A, 2)
  end
  print(string.format("fire %d %04X", n, ids[i]))
end)
