-- Make the game draw every projectile kind, out of shells the AI really
-- throws.  A projectile block ($1A00, $1A80; docs/ITEMS.md section 5)
-- carries its VARIANT at +$70 (v*2) and its handler record at +$6E
-- ($80:EED8[v]); variants 5 and 6 share one record and are the CPU
-- specials, told apart by the OWNER (+$6A, the kart block).  So: when a
-- block goes live, rewrite variant + record + owner from a schedule, and
-- dump objects + VRAM for the next 40 frames.
--   OUT=prefix
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ppu = manager.machine.devices[":ppu"]
local OUT = os.getenv("OUT") or "tmp/fp"
local NOPATCH = tonumber(os.getenv("NOPATCH") or "0")   -- 1: dump every real spawn untouched
local STEP = tonumber(os.getenv("STEP") or "2")
local REC = {0xF6E3,0xF6EB,0xF6F3,0xF6EB,0xF6DB,0xF6FB,0xF6FB,0xF6E3}   -- $80:EED8
local SCHED = {}
for k = 1, 7 do SCHED[#SCHED+1] = {5, 0x1000 + k*0x100} end
for k = 1, 7 do SCHED[#SCHED+1] = {6, 0x1000 + k*0x100} end
SCHED[#SCHED+1] = {0, 0x1000}; SCHED[#SCHED+1] = {4, 0x1000}; SCHED[#SCHED+1] = {7, 0x1000}
local n, si, watch, live = 0, 1, nil, {}
local function it(nm) return emu.item(ppu.items[nm]) end
local function dumpb(nm, path)
  local i = it(nm); local f = io.open(path,"wb"); local b={}
  for a=0,i.count-1 do local v=i:read(a)
    if i.size==2 then b[#b+1]=string.char(v&0xFF,(v>>8)&0xFF) else b[#b+1]=string.char(v&0xFF) end
    if #b==4096 then f:write(table.concat(b)); b={} end end
  f:write(table.concat(b)); f:close()
end
local function objdump(path, fr)
  local ox,oy,oc,op,osz,ons,oh,ov = it("0/m_objects.x"),it("0/m_objects.y"),
    it("0/m_objects.character"),it("0/m_objects.pal"),it("0/m_objects.size"),
    it("0/m_objects.name_select"),it("0/m_objects.hflip"),it("0/m_objects.vflip")
  local f = io.open(path,"w"); f:write(string.format("# frame %d\n", fr))
  for i=0,127 do f:write(string.format("%d %d %d %d %d %d %d %d %d 0\n", i,
    ox:read(i),oy:read(i),oc:read(i),op:read(i),osz:read(i),ons:read(i),oh:read(i),ov:read(i))) end
  f:close()
end
local grabbed = false
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) ~= 2 then return end
  if not grabbed then grabbed = true; dumpb("0/m_cgram", OUT..".cgram") end
  for _, b in ipairs({0x7E1A00, 0x7E1A80}) do
    local act = (mem:read_u16(b + 0x12) & 0x8000) ~= 0
    if act and not live[b] then
      live[b] = true
      local owner, var = mem:read_u16(b + 0x6A), mem:read_u16(b + 0x70) // 2
      print(string.format("f%d spawn at $%04X: variant %d owner $%04X (char field $%02X)  block xy (%d,%d) owner xy (%d,%d)",
        n, b & 0xFFFF, var, owner, mem:read_u8(0x7E0000 + owner + 0xC0),
        mem:read_u16(b + 0x18), mem:read_u16(b + 0x1C), mem:read_u16(0x7E0000 + owner + 0x18), mem:read_u16(0x7E0000 + owner + 0x1C)))
      if NOPATCH ~= 0 and not watch then
        watch = {b = b, f0 = n, tag = string.format("real_f%d_v%d", n, var)}
      elseif not watch and si <= #SCHED then
        local v, own = SCHED[si][1], SCHED[si][2]
        mem:write_u16(b + 0x70, v * 2); mem:write_u16(b + 0x6E, REC[v + 1]); mem:write_u16(b + 0x6A, own)
        watch = {b = b, f0 = n, v = v, own = own, tag = string.format("v%d_o%02X", v, own >> 8)}
        print(string.format("   PATCHED -> variant %d owner $%04X (char field $%02X)  tag %s", v, own, mem:read_u8(0x7E0000 + own + 0xC0), watch.tag))
        si = si + 1
      end
    elseif not act then live[b] = false end
  end
  if watch then
    local k = n - watch.f0
    if k % STEP == 0 and k <= 40 then
      objdump(string.format("%s.%s.%03d.obj", OUT, watch.tag, k), n)
      dumpb("0/m_vram", string.format("%s.%s.%03d.vram", OUT, watch.tag, k))
    end
    if k > 40 then watch = nil end
  end
  if NOPATCH == 0 and si > #SCHED and not watch then manager.machine:exit() end
end)
