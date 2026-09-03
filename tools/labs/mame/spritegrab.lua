-- VRAM + CGRAM + the 128 decoded sprites, at one frame of a recording.
--
--   WANT=mole|fish|thwomp|any  OUT=path  [FROM=frame] [EXACT=frame]
--
-- vramgrab.lua takes the tiles; this also takes the OBJECT TABLE, which
-- is the link NOTES 271 needs: it says which tiles a sprite is actually
-- BUILT from, so the hunt no longer has to guess from a VRAM diff.  (The
-- diff is what went wrong in NOTES 269 - twelve frames of it caught kart
-- rotation re-uploads, so the "58 mole tiles" were mostly kart.)
--
-- MAME keeps no raw m_oam array: it decodes OAM into m_objects.{x,y,
-- character,pal,size,hflip,vflip,pri,name_select}, 128 entries each,
-- with the OBJ character base in m_oam.tile_data_address.  Those are
-- save-state items, read through emu.item(dev.items[name]):read(i) -
-- ppu.spaces["vram"] does not exist in this build.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ppu = manager.machine.devices[":ppu"]
local want  = os.getenv("WANT") or "mole"
local out   = os.getenv("OUT")  or "tmp/sprite_grab"
local from  = tonumber(os.getenv("FROM") or "0")
local exact = tonumber(os.getenv("EXACT") or "0")
local n, done = 0, false

local function ready()
  if exact > 0 then return n >= exact end
  if mem:read_u8(0x7E0036) ~= 2 then return false end
  if n < from then return false end
  if want == "mole" then return mem:read_u16(0x7E1050) ~= 0 end
  if want == "any"  then return true end
  return mem:read_u16(0x7E10EA) > 0x200
end

local function it(name) return emu.item(ppu.items[name]) end

local function dump_bytes(name, path)
  local i = it(name)
  local f = io.open(path, "wb")
  local buf = {}
  for a = 0, i.count - 1 do
    local v = i:read(a)
    if i.size == 2 then
      buf[#buf + 1] = string.char(v & 0xFF, (v >> 8) & 0xFF)
    else
      buf[#buf + 1] = string.char(v & 0xFF)
    end
    if #buf == 4096 then f:write(table.concat(buf)); buf = {} end
  end
  f:write(table.concat(buf)); f:close()
  return i.count
end

emu.register_frame_done(function()
  n = n + 1
  if done or not ready() then return end
  done = true
  dump_bytes("0/m_vram",  out .. ".vram")
  dump_bytes("0/m_cgram", out .. ".cgram")
  local ox, oy = it("0/m_objects.x"), it("0/m_objects.y")
  local oc, op = it("0/m_objects.character"), it("0/m_objects.pal")
  local os_, ons = it("0/m_objects.size"), it("0/m_objects.name_select")
  local oh, ov = it("0/m_objects.hflip"), it("0/m_objects.vflip")
  local opr = it("0/m_objects.pri")
  local base = it("0/m_oam.tile_data_address"):read(0)
  local nsel = it("0/m_oam.name_select"):read(0)
  local bsz  = it("0/m_oam.base_size"):read(0)
  local f = io.open(out .. ".obj", "w")
  f:write(string.format("# frame %d  base %04X  name_select %d  base_size %d\n",
                        n, base, nsel, bsz))
  f:write("# i x y char pal size nsel hflip vflip pri\n")
  for i = 0, 127 do
    f:write(string.format("%d %d %d %d %d %d %d %d %d %d\n", i,
      ox:read(i), oy:read(i), oc:read(i), op:read(i), os_:read(i),
      ons:read(i), oh:read(i), ov:read(i), opr:read(i)))
  end
  f:close()
  print(string.format("grabbed %s at frame %d -> %s.{vram,cgram,obj}  base=%04X nsel=%d size=%d",
    want, n, out, base, nsel, bsz))
  manager.machine:exit()
end)
