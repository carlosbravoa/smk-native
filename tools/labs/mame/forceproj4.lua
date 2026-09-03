-- One real spawn, its OWNER (+$6A) rewritten to another kart the frame it
-- appears - nothing else touched - so the special draws as that kart's
-- character would draw it.  The spawner links the block into the owner's
-- collision list, so only the DRAWING should follow the new owner.
--   AT=frame OWNER=0x1x00 OUT=prefix
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ppu = manager.machine.devices[":ppu"]
local AT, OWNER, OUT = tonumber(os.getenv("AT")), tonumber(os.getenv("OWNER")), os.getenv("OUT") or "tmp/f4"
local n, watch, live = 0, nil, {}
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
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) ~= 2 then return end
  if n == AT - 30 then dumpb("0/m_cgram", OUT..".cgram") end
  for _, b in ipairs({0x7E1A00, 0x7E1A80}) do
    local act = (mem:read_u16(b + 0x12) & 0x8000) ~= 0
    if act and not live[b] and n >= AT - 2 and n <= AT + 2 and not watch then
      mem:write_u16(b + 0x6A, OWNER)
      watch = {f0 = n}
      print(string.format("f%d owner of $%04X -> $%04X", n, b & 0xFFFF, OWNER))
    end
    live[b] = act
  end
  if watch then
    local k = n - watch.f0
    if k % 2 == 0 and k <= 40 then objdump(string.format("%s.%03d.obj", OUT, k), n); dumpb("0/m_vram", string.format("%s.%03d.vram", OUT, k)) end
    if k > 40 then manager.machine:exit() end
  end
  if n > AT + 60 then manager.machine:exit() end
end)
