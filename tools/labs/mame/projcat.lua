-- Every 8x8 sprite drawn from the PROJECTILE ROW (name table 0, chars
-- $E0..$FF = VRAM $4E0..$4FF, NOTES 272) over a whole recording: first
-- frame, palette, position.  Plus one VRAM+CGRAM grab, at the first
-- in-race frame + 300, so each char can be rendered and looked up.
--   OUT=prefix
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ppu = manager.machine.devices[":ppu"]
local OUT = os.getenv("OUT") or "tmp/pc"
local n, race0, seen, grabbed = 0, nil, {}, false
local function it(nm) return emu.item(ppu.items[nm]) end
local function dumpb(nm, path)
  local i = it(nm); local f = io.open(path,"wb"); local b={}
  for a=0,i.count-1 do local v=i:read(a)
    if i.size==2 then b[#b+1]=string.char(v&0xFF,(v>>8)&0xFF) else b[#b+1]=string.char(v&0xFF) end
    if #b==4096 then f:write(table.concat(b)); b={} end end
  f:write(table.concat(b)); f:close()
end
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) ~= 2 then return end
  if not race0 then race0 = n end
  if not grabbed and n >= race0 + 300 then
    grabbed = true
    dumpb("0/m_vram", OUT..".vram"); dumpb("0/m_cgram", OUT..".cgram")
  end
  local oc,op,osz,ons,oy,ox = it("0/m_objects.character"),it("0/m_objects.pal"),
    it("0/m_objects.size"),it("0/m_objects.name_select"),it("0/m_objects.y"),it("0/m_objects.x")
  for i=0,127 do
    local y = oy:read(i)
    if y > 0 and y < 225 and osz:read(i) == 0 and ons:read(i) == 0 and oc:read(i) >= 0xE0 then
      local k = string.format("$%02X p%d", oc:read(i), op:read(i))
      if not seen[k] then
        seen[k] = true
        print(string.format("proj %s first f%d at (%d,%d)", k, n, ox:read(i), y))
      end
    end
  end
end)
