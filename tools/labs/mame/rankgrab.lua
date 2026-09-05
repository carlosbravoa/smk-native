-- The finishing list, out of the cc150 recording (NOTES 282): OAM, VRAM,
-- CGRAM and a snapshot at one frame, for tools/labs/spritesrc.py.
--   EXACT=7200 OUT=tmp/rank tools/labs/mame/replay.sh cc150 tools/labs/mame/rankgrab.lua 130
-- at frame EXACT: OAM list, VRAM, CGRAM and a snapshot, for the finishing list
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ppu = manager.machine.devices[":ppu"]
local want = tonumber(os.getenv("EXACT") or "0")
local out  = os.getenv("OUT") or "tmp/rank"
local n = 0
local function item(k) local i = ppu.items[k]; return i and emu.item(i) or nil end
emu.register_frame_done(function()
  n = n + 1
  if n ~= want then return end
  local ch, ns = item("0/m_objects.character"), item("0/m_objects.name_select")
  local vx, vy = item("0/m_objects.x"), item("0/m_objects.y")
  local sz, pal = item("0/m_objects.size"), item("0/m_objects.pal")
  local hf, vf, pri = item("0/m_objects.hflip"), item("0/m_objects.vflip"), item("0/m_objects.priority_bits")
  local f = io.open(out .. ".oam", "w")
  for i = 0, ch.count - 1 do
    f:write(string.format("%d x%d y%d sz%d tile%02X ns%d pal%s hf%s pri%s\n", i,
      vx:read(i), vy:read(i), sz:read(i), ch:read(i), ns:read(i),
      pal and pal:read(i) or "?", hf and hf:read(i) or "?", pri and pri:read(i) or "?"))
  end
  f:close()
  local vr = emu.item(ppu.items["0/m_vram"])
  local g = io.open(out .. ".vram", "wb")
  local buf = {}
  for a = 0, vr.count - 1 do
    buf[#buf+1] = string.char(vr:read(a) & 0xFF)
    if #buf == 4096 then g:write(table.concat(buf)); buf = {} end
  end
  g:write(table.concat(buf)); g:close()
  local cg = emu.item(ppu.items["0/m_cgram"])
  local c = io.open(out .. ".cgram", "wb")
  for a = 0, cg.count - 1 do local v = cg:read(a); c:write(string.char(v & 0xFF, (v >> 8) & 0xFF)) end
  c:close()
  -- the tile-data base for sprites, and the BG3 tilemap of the HUD
  local ob = item("0/m_oam.tile_data_address")
  print(string.format("obsel tile base %s", ob and tostring(ob:read(0)) or "?"))
  local scr = manager.machine.screens[":screen"]
  if scr then scr:snapshot(out .. ".png") end
  for i = 1, 3 do manager.machine.video:frame_update() end
  manager.machine:exit()
end)
