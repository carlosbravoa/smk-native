-- FORCE the squash (bug 13) that no recording contains, in the real game.
--
--   WHO=player|ai  FROM=frame  OUT=prefix
--
-- The flatten is the hit-while-small path (NOTES 204/272).  The lightning
-- handler at $80:EA3B shrinks a kart with `$E2|=$300, $E4=$1000, $8C|=3,
-- $84=$440` (docs/ITEMS.md), so do exactly that to one kart, then stand
-- it on another every frame.  WHO=player shrinks P1 and parks it on the
-- nearest moving AI kart; WHO=ai shrinks kart 1 and parks it on P1.
-- Every 5 frames: the decoded object table and VRAM, so the frame the
-- kart's tiles change can be found afterwards and looked up in the ROM.
-- Kart blocks are $7E:1000 + k*$100; $18/$1C x/y pixel, $16/$1A fraction.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ppu = manager.machine.devices[":ppu"]
local WHO  = os.getenv("WHO") or "player"      -- player | ai | thwomp
local E2   = tonumber(os.getenv("E2") or "1")     -- 0: shrink WITHOUT the lightning's spin bits
local EVERY= tonumber(os.getenv("EVERY") or "1")  -- park every N frames (1 = hold it there)
local LEN  = tonumber(os.getenv("LEN") or "400")
local RELEASE = tonumber(os.getenv("RELEASE") or "0") -- thwomp: stop parking once the block has hit (E2 bit 10)
local released = false
local FROM = tonumber(os.getenv("FROM") or "2500")
local OUT  = os.getenv("OUT") or "tmp/fsq"
local n, dumped = 0, 0
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
local function s16(v) if v > 32767 then return v - 65536 end return v end
local function shrink(b)
  if E2 ~= 0 then mem:write_u16(b+0xE2, mem:read_u16(b+0xE2) | 0x300) end
  mem:write_u16(b+0xE4, 0x1000)
  mem:write_u16(b+0x8C, mem:read_u16(b+0x8C) | 3)
  mem:write_u16(b+0x84, 0x440)
end
local function park(src, dst)      -- put kart `src` where kart `dst` is
  mem:write_u16(src+0x16, mem:read_u16(dst+0x16)); mem:write_u16(src+0x18, mem:read_u16(dst+0x18))
  mem:write_u16(src+0x1A, mem:read_u16(dst+0x1A)); mem:write_u16(src+0x1C, mem:read_u16(dst+0x1C))
end
local prev = {}
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) ~= 2 or n < FROM then return end
  local P1 = 0x7E1000
  if n == FROM then
    if WHO == "player" then shrink(P1) elseif WHO == "ai" then shrink(0x7E1100) end
    dumpb("0/m_cgram", OUT..".cgram")
    print(string.format("f%d shrank %s", n, WHO))
  end
  if WHO == "thwomp" and RELEASE ~= 0 and (mem:read_u16(P1+0xE2) & 0x400) ~= 0 then released = true end
  if WHO == "thwomp" and not released then
    -- entity block 0's world position, +$18/+$1C (NOTES 272); hold P1 on it
    local ex, ey = mem:read_u16(0x7E1818), mem:read_u16(0x7E181C)
    if ex > 0 and ey > 0 then
      mem:write_u16(P1+0x16, 0); mem:write_u16(P1+0x18, ex)
      mem:write_u16(P1+0x1A, 0); mem:write_u16(P1+0x1C, ey)
      mem:write_u16(P1+0xEA, 0)
    end
  elseif (n - FROM) % EVERY ~= 0 then
    -- not a parking frame
  elseif WHO == "player" then
    local px, py = s16(mem:read_u16(P1+0x18)), s16(mem:read_u16(P1+0x1C))
    local best, bd = nil, 1 << 30
    for k = 1, 7 do
      local b = 0x7E1000 + k * 0x100
      if math.abs(s16(mem:read_u16(b+0xEA))) >= 32 then
        local x, y = s16(mem:read_u16(b+0x18)), s16(mem:read_u16(b+0x1C))
        local d = (x-px)^2 + (y-py)^2
        if d < bd then bd, best = d, b end
      end
    end
    if best then park(P1, best) end
  else
    park(0x7E1100, P1)
  end
  for k = 0, 1 do
    local b = 0x7E1000 + k * 0x100
    local key = string.format("%04X %04X %04X", mem:read_u16(b+0xA6), mem:read_u16(b+0xE2), mem:read_u16(b+0x8C))
    if prev[k] ~= key then
      print(string.format("f%d kart %d: state $%04X $E2 $%04X $8C $%04X $84 $%04X", n, k,
        mem:read_u16(b+0xA6), mem:read_u16(b+0xE2), mem:read_u16(b+0x8C), mem:read_u16(b+0x84)))
      prev[k] = key
    end
  end
  if (n - FROM) % 5 == 0 and dumped < LEN // 5 then
    dumped = dumped + 1
    objdump(string.format("%s.%06d.obj", OUT, n), n)
    dumpb("0/m_vram", string.format("%s.%06d.vram", OUT, n))
  end
  if n > FROM + LEN then manager.machine:exit() end
end)
