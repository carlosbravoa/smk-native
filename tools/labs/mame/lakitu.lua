-- Lakitu and his traffic light: what is on screen during the countdown.
-- Dump OAM plus the sprite half of VRAM at a frame in the middle of the
-- lights, so the art can be found and matched to a ROM asset.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local ppu = manager.machine.devices[":ppu"]
local done = false
emu.register_frame_done(function()
  if done then return end
  if mem:read_u8(0x7E0036) ~= 2 then return end
  local c = mem:read_u16(0x7E0146)
  if c < 0xFF00 or c > 0xFFC0 then return end     -- ~64..256 frames to go
  done = true
  print("COUNTDOWN $0146 = " .. string.format("$%04X", c))
  local oam = manager.machine.devices[":ppu"] and nil
  -- OAM through the debugger-visible space is not exposed; use the PPU's
  -- own memory share if present
  local shares = manager.machine.memory.shares
  for name, s in pairs(shares) do print("SHARE " .. name .. " size " .. s.size) end
  manager.machine:exit()
end)
