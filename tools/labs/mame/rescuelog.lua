-- Lakitu's rescue as the player meets it: the kart's hazard state $A0,
-- its height, position and speed, per frame.
--
-- Attach it to a RECORDING, not a savestate:
--   tools/labs/mame/replay.sh <name> tools/labs/mame/rescuelog.lua 60
--
-- Savestates do not work here.  `-state N` and Lua's machine:load() both
-- make MAME exit silently, with no message on either stream and status 0,
-- for every .sta in the repo as well as a fresh one - so it is this
-- environment, not the file.  `-playback` is the format that works.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function s(v) if v > 32767 then return v - 65536 end return v end
local n = 0
print("frame,A0,z,x,y,speed")
emu.register_frame_done(function()
  n = n + 1
  print(string.format("%d,%04X,%d,%d,%d,%d", n, w(0x10A0),
        s(w(0x1020)), w(0x1018), w(0x101C), s(w(0x10EA))))
end)
