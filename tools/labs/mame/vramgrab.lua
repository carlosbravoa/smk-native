-- Dump VRAM (and CGRAM) at the first frame a condition holds, so an
-- entity's LIVE tiles can be taken out of the running game and then
-- searched for in the ROM (tools/labs/findart.py).
--
--   WANT=mole|fish|thwomp   OUT=path   [FROM=frame]
--
-- mole:   the kart's $50 holds the mole block's address while one rides
-- fish:   Koopa Beach - grab once the race is up and moving
-- thwomp: any frame of a Bowser Castle / Rainbow Road run
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ppu = manager.machine.devices[":ppu"]
local want = os.getenv("WANT") or "mole"
local out  = os.getenv("OUT")  or "tmp/vram_grab.bin"
local from = tonumber(os.getenv("FROM") or "0")
local exact = tonumber(os.getenv("EXACT") or "0")   -- grab at this frame, whatever the state
local n, done = 0, false

local function ready()
  if exact > 0 then return n >= exact end
  if mem:read_u8(0x7E0036) ~= 2 then return false end
  if n < from then return false end
  if want == "mole"   then return mem:read_u16(0x7E1050) ~= 0 end
  if want == "nomole" then return mem:read_u16(0x7E1050) == 0
                              and mem:read_u16(0x7E10EA) > 0x200 end
  return mem:read_u16(0x7E10EA) > 0x200         -- up to speed
end

emu.register_frame_done(function()
  n = n + 1
  if done or not ready() then return end
  done = true
  -- VRAM is not an address space in this build.  It is a SAVE-STATE item:
  -- device.items[name] gives an INDEX, emu.item(index) the object, and
  -- :read(i) one byte of it.  (ppu.spaces["vram"] is nil here, which is
  -- why the first attempt wrote an empty file.)
  local vr = emu.item(ppu.items["0/m_vram"])
  local cg = emu.item(ppu.items["0/m_cgram"])
  local f = io.open(out, "wb")
  local buf = {}
  for a = 0, vr.count - 1 do
    buf[#buf+1] = string.char(vr:read(a) & 0xFF)
    if #buf == 4096 then f:write(table.concat(buf)); buf = {} end
  end
  f:write(table.concat(buf)); f:close()
  local g = io.open(out .. ".cgram", "wb")
  for a = 0, cg.count - 1 do g:write(string.char(cg:read(a) & 0xFF)) end
  g:close()
  print(string.format("grabbed %s at frame %d -> %s ($50 = %04X, spd %d)",
    want, n, out, mem:read_u16(0x7E1050), mem:read_u16(0x7E10EA)))
  manager.machine:exit()
end)
