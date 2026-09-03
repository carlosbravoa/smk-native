-- Rainbow Road (cup 3 course 4 poked in, tools/labs/mame/rrpal.lua): the
-- Thwomp's palette, EVERY frame of a window, for the flash cycle.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ppu = manager.machine.devices[":ppu"]
local FROM, TO = tonumber(os.getenv("FROM") or "2780"), tonumber(os.getenv("TO") or "2900")
local n = 0
local function it(nm) return emu.item(ppu.items[nm]) end
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) ~= 2 then mem:write_u16(0x7E0150, 3); mem:write_u16(0x7E0152, 4); return end
  if n < FROM then return end
  if n > TO then manager.machine:exit(); return end
  local oc,op,osz,ons,oy = it("0/m_objects.character"),it("0/m_objects.pal"),it("0/m_objects.size"),it("0/m_objects.name_select"),it("0/m_objects.y")
  local s = {}
  for i=0,127 do
    local y = oy:read(i)
    if y > 0 and y < 225 and ons:read(i) == 0 and oc:read(i) >= 0xC0 and osz:read(i) == 1 then
      s[#s+1] = string.format("$%02X:p%d", oc:read(i), op:read(i))
    end
  end
  if #s > 0 then print(string.format("f%d %s", n, table.concat(s, " "))) end
end)
