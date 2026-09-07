-- every visible OAM entry in [FROM,TO], plus the kart's $1050/$1084/$1018/$101C and the entity blocks
local ppu = manager.machine.devices[":ppu"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function r16(a) return mem:read_u16(0x7E0000 + a) end
local from, to = tonumber(os.getenv("FROM") or "0"), tonumber(os.getenv("TO") or "0")
local n = 0
local function item(k) local i = ppu.items[k]; return i and emu.item(i) or nil end
emu.register_frame_done(function()
  n = n + 1
  if n < from or n > to then return end
  local ch, ns = item("0/m_objects.character"), item("0/m_objects.name_select")
  local vx, vy, sz, pal, hf = item("0/m_objects.x"), item("0/m_objects.y"), item("0/m_objects.size"), item("0/m_objects.pal"), item("0/m_objects.hflip")
  local parts = {}
  for i = 0, ch.count - 1 do
    local y = vy:read(i)
    if y < 224 and vx:read(i) < 256 then
      parts[#parts+1] = string.format("%d:%d:%d:%d:%02X:%d:%d:%d", i, vx:read(i), y, sz:read(i), ch:read(i), ns:read(i), pal:read(i), hf:read(i))
    end
  end
  local e = {}
  for _, base in ipairs({0x1800, 0x1840, 0x1880, 0x18C0}) do
    e[#e+1] = string.format("%04X:%04X:%04X:%04X:%04X", r16(base), r16(base + 0x1E), r16(base + 0x20), r16(base + 0x04), r16(base + 0x08))
  end
  print(string.format("%d|%d|%d|%d|%d|%d|%s|%s", n, r16(0x1050), r16(0x1084), r16(0x1018), r16(0x101C), r16(0x10EA), table.concat(e, ","), table.concat(parts, " ")))
end)
