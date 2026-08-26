-- The whole kart block through the start window, so the rev accumulator
-- can be found by diffing a normal launch against a turbo one.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local bin = io.open(os.getenv("STARTBLK") or "startblk.bin", "wb")
local race, inrace, t = 0, false, 0
emu.register_frame_done(function()
  local mode = mem:read_u8(0x7E0036)
  if mode == 2 and not inrace then inrace = true; race = race + 1; t = 0
  elseif mode ~= 2 and inrace then inrace = false end
  if not inrace then return end
  if t <= 400 then
    local s = {}
    for a = 0x00, 0xFE, 2 do s[#s+1] = string.pack("<I2", mem:read_u16(0x7E1000 + a)) end
    bin:write(string.pack("<I2I2", race, t) .. table.concat(s))
  end
  t = t + 1
end)
emu.register_stop(function() bin:close() end)
