-- Every kart, every race frame: character, rank, row, speed, progress,
-- position - so the original's AI can be scored per row and per lap.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function s(v) if v > 32767 then return v - 65536 end return v end
local out = io.open(os.getenv("AILOG") or "ailog.csv", "w")
local hdr = {"f","class","mode"}
for k = 0, 7 do
  for _, c in ipairs({"ch","rk","row","spd","c0","x","y","coins","e0"}) do hdr[#hdr+1] = "k"..k..c end
end
out:write(table.concat(hdr, ",") .. "\n")
local n = 0
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036)
  if m ~= 2 then return end
  local row = {tostring(n), tostring(mem:read_u8(0x7E0128)), tostring(m)}
  for k = 0, 7 do
    local b = 0x1000 + k * 0x100
    row[#row+1] = string.format("%d,%d,%d,%d,%d,%d,%d,%d,%d", w(b+0x12)//2, w(b+0xE6)//2,
      w(b+0xC8) & 0xFF, s(w(b+0xEA)), w(b+0xC0), w(b+0x18), w(b+0x1C), w(0x0E00 + k*2), w(b+0xE0))
  end
  out:write(table.concat(row, ",") .. "\n")
end)
