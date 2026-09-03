-- Start a race on a course the recording never chose: poke the cup and
-- course words ($0150 / $0152, the ones tools/labs/dmalist.py overrides)
-- every frame until the race is running, then catalogue the theme
-- entity's sprites (table 0, chars $C0..) with their palette, dump VRAM
-- and CGRAM once, and report the words the race actually ran with.
--   CUP=n COURSE=n OUT=prefix
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ppu = manager.machine.devices[":ppu"]
local CUP, COURSE = tonumber(os.getenv("CUP") or "3"), tonumber(os.getenv("COURSE") or "4")
local OUT = os.getenv("OUT") or "tmp/rr"
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
  if mem:read_u8(0x7E0036) ~= 2 then
    mem:write_u16(0x7E0150, CUP); mem:write_u16(0x7E0152, COURSE)
    return
  end
  if not race0 then
    race0 = n
    print(string.format("race at f%d: $0150=%04X $0152=%04X $0124=%04X $0126=%04X", n,
      mem:read_u16(0x7E0150), mem:read_u16(0x7E0152), mem:read_u16(0x7E0124), mem:read_u16(0x7E0126)))
  end
  if not grabbed and n >= race0 + 300 then grabbed = true; dumpb("0/m_vram", OUT..".vram"); dumpb("0/m_cgram", OUT..".cgram") end
  local oc,op,osz,ons,oy,ox = it("0/m_objects.character"),it("0/m_objects.pal"),
    it("0/m_objects.size"),it("0/m_objects.name_select"),it("0/m_objects.y"),it("0/m_objects.x")
  for i=0,127 do
    local y = oy:read(i)
    if y > 0 and y < 225 and ons:read(i) == 0 and oc:read(i) >= 0xC0 and osz:read(i) == 1 then
      local k = string.format("$%02X p%d", oc:read(i), op:read(i))
      if not seen[k] then seen[k] = true; print(string.format("ent %s first f%d at (%d,%d)", k, n, ox:read(i), y)) end
    end
  end
  if n > race0 + 3000 then manager.machine:exit() end
end)
