local cpu = manager.machine.devices[":soundcpu"]
local prog = cpu.spaces["program"]
local dat = cpu.spaces["data"]
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n ~= 3000 then return end
  local p, d = {}, {}
  for i = 0xF0, 0xFF do p[#p+1] = string.format("%02X", prog:read_u8(i)) end
  if dat then for i = 0xF0, 0xFF do d[#d+1] = string.format("%02X", dat:read_u8(i)) end end
  print("PROG F0-FF: " .. table.concat(p, " "))
  print("DATA F0-FF: " .. (dat and table.concat(d, " ") or "no data space"))
  manager.machine:exit()
end)
