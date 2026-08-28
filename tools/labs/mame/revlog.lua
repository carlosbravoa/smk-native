-- The rev machine as the PLAYER drives it: $C2 (the rev), $E0 (the turbo
-- window bit), $E2 (bit 0 the wheelspin flag, bit 5 the smoke), $EA speed,
-- $EE accel and $AC the drive state, against the countdown $0146.
-- Replay the user's own `starts` recording under it (NOTES 143/145).
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function s(v) if v > 32767 then return v - 65536 end return v end
print("frame,c146,c142,rev,E0,E2,speed,EE,AC,pad")
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) // 2 ~= 6 and mem:read_u8(0x7E0036) // 2 ~= 1 then return end
  print(string.format("%d,%d,%d,%d,%04X,%04X,%d,%d,%04X,%04X",
    n, s(w(0x0146)), s(w(0x0142)), s(w(0x10C2)), w(0x10E0), w(0x10E2),
    s(w(0x10EA)), s(w(0x10EE)), w(0x10AC), w(0x10C4)))
end)
