-- The parsed OAM at one frame: every sprite's position, size and TILE.
-- MAME's SNES PPU exposes OAM as save-state items (0/m_objects.*), which
-- is the only way to it in this build - there is no "oam" address space.
--   EXACT=frame   OUT=path
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ppu = manager.machine.devices[":ppu"]
local want = tonumber(os.getenv("EXACT") or "0")
local out  = os.getenv("OUT") or "tmp/oam.txt"
local n = 0
local function item(k) local i = ppu.items[k]; return i and emu.item(i) or nil end
emu.register_frame_done(function()
  n = n + 1
  if n ~= want then return end
  local ch, ns = item("0/m_objects.character"), item("0/m_objects.name_select")
  local vx, vy = item("0/m_objects.x"), item("0/m_objects.y")
  local sz = item("0/m_objects.size")
  local f = io.open(out, "w")
  f:write(string.format("# frame %d  $50=%04X  obsel base=%s\n", n,
    mem:read_u16(0x7E1050), tostring(item("0/m_oam.tile_data_address") and
    item("0/m_oam.tile_data_address"):read(0) or "?")))
  for i = 0, (ch and ch.count or 0) - 1 do
    f:write(string.format("%d %d %d %d %d %d\n", i,
      vx and vx:read(i) or -1, vy and vy:read(i) or -1,
      sz and sz:read(i) or -1, ch:read(i), ns and ns:read(i) or 0))
  end
  f:close()
  print(string.format("oam at frame %d -> %s (%d sprites)", n, out, ch and ch.count or 0))
  manager.machine:exit()
end)
