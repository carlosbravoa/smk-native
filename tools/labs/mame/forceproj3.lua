-- SYNTHESISE projectile spawns, so the CPU specials that every recording
-- throws off-screen get drawn in front of the camera.  The first real
-- spawn's whole 128-byte block ($1A00 or $1A80, docs/ITEMS.md section 5)
-- is copied; then every 100 frames it is written back into block $1A80
-- with the OWNER (+$6A) set to kart K, VARIANT (+$70) 5, record (+$6E)
-- $F6FB ($80:EED8[5]), and the position (+$16..+$1C, the kart layout)
-- a little ahead of player 1.  Objects + VRAM dumped for 40 frames each.
--   OUT=prefix FROM=frame
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ppu = manager.machine.devices[":ppu"]
local OUT = os.getenv("OUT") or "tmp/fs"
local FROM = tonumber(os.getenv("FROM") or "3000")
local n, saved, si, watch, live = 0, nil, 1, nil, {}
local OWNERS = {0x1100,0x1200,0x1300,0x1400,0x1500,0x1600,0x1700}
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
local grabbed, lastsyn = false, -1000
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) ~= 2 then return end
  if not grabbed then grabbed = true; dumpb("0/m_cgram", OUT..".cgram") end
  for _, b in ipairs({0x7E1A00, 0x7E1A80}) do
    local act = (mem:read_u16(b + 0x12) & 0x8000) ~= 0
    if act and not live[b] and not saved then
      saved = {}
      for i = 0, 127 do saved[i] = mem:read_u8(b + i) end
      print(string.format("f%d captured a real spawn at $%04X variant %d", n, b & 0xFFFF, mem:read_u16(b+0x70)//2))
    end
    live[b] = act
  end
  if saved and not watch and n >= FROM and n - lastsyn >= 100 and si <= #OWNERS then
    local b = 0x7E1A80
    for i = 0, 127 do mem:write_u8(b + i, saved[i]) end
    local own = OWNERS[si]
    mem:write_u16(b + 0x6A, own); mem:write_u16(b + 0x70, 10); mem:write_u16(b + 0x6E, 0xF6FB)
    -- just ahead of P1, along its heading is unknown here: use P1's spot
    mem:write_u16(b + 0x16, mem:read_u16(0x7E1016)); mem:write_u16(b + 0x18, mem:read_u16(0x7E1018))
    mem:write_u16(b + 0x1A, mem:read_u16(0x7E101A)); mem:write_u16(b + 0x1C, mem:read_u16(0x7E101C))
    mem:write_u16(b + 0x12, 0x8000)
    watch = {f0 = n, tag = string.format("syn_o%02X", own >> 8)}
    print(string.format("f%d synthesised variant 5 owner $%04X (char field $%02X)", n, own, mem:read_u8(0x7E0000 + own + 0xC0)))
    si = si + 1; lastsyn = n
  end
  if watch then
    local k = n - watch.f0
    if k % 2 == 0 and k <= 40 then
      objdump(string.format("%s.%s.%03d.obj", OUT, watch.tag, k), n)
      dumpb("0/m_vram", string.format("%s.%s.%03d.vram", OUT, watch.tag, k))
    end
    if k > 40 then watch = nil end
  end
  if si > #OWNERS and not watch then manager.machine:exit() end
end)
