-- Every OAM entry, every frame in [FROM, TO] - Lakitu's rows and the
-- sink's sprites came from this (NOTES 283).
--   FROM=3250 TO=3900 tools/labs/mame/replay.sh underwater1 tools/labs/mame/oamlog.lua 66
-- every OAM entry, every frame in [FROM,TO]
local ppu = manager.machine.devices[":ppu"]
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
  print(n .. " " .. table.concat(parts, " "))
end)
