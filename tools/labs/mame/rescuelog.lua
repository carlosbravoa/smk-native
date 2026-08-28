-- Lakitu's rescue from the user's own savestate (~/.mame/sta/snes/1.sta):
-- the kart's hazard state $A0, its height, position and speed, per frame.
-- The state is loaded from Lua rather than -state, which exits silently.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function s(v) if v > 32767 then return v - 65536 end return v end
local n, loaded = 0, false
emu.register_frame_done(function()
  n = n + 1
  if n == 5 then manager.machine:load("1"); loaded = true; return end
  if not loaded or n < 8 then return end
  if n == 8 then print("frame,A0,z,x,y,speed,lapword") end
  print(string.format("%d,%04X,%d,%d,%d,%d,%04X", n, w(0x10A0), s(w(0x101E)),
        w(0x1018), w(0x101C), s(w(0x10EA)), w(0x10C0)))
end)
