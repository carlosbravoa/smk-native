-- ALL 32 words of entity block $1800 (and its pair $1840), every frame,
-- f14400-14700 of the moles recording: where does the pop live?
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n < 14400 or n > 14700 then return end
  local o = {}
  for i = 0, 0x3E, 2 do o[#o+1] = string.format("%04X", w(0x1800 + i)) end
  local p = {}
  for i = 0, 0x3E, 2 do p[#p+1] = string.format("%04X", w(0x1840 + i)) end
  print(string.format("%d|%s|%s", n, table.concat(o, " "), table.concat(p, " ")))
end)
