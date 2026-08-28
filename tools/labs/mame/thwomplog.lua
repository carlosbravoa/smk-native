-- Every Thwomp height, and every impact, through a recorded race.
--
-- Seven rigs failed to answer "at what height does a Thwomp stop being
-- solid" (NOTES 176).  Each one teleported a kart at an object and each
-- measured something else: two could not detect a collision at all, one
-- never covered the gap, one never let the object's cycle advance, one
-- approached from off the road and measured the WALL, and one approached
-- along the flow field and never arrived.
--
-- A person driving needs none of that.  Drive at the Thwomps - into them
-- when they are down, under them at every height on the way up - and this
-- logs the object heights next to the kart's own impact state, so the
-- threshold is read off natural driving instead of a rig.
--
--   tools/labs/mame/play.sh thwomp        (Bowser Castle, a few laps)
--   tools/labs/mame/replay.sh thwomp tools/labs/mame/thwomplog.lua 400
--
-- $10 bit $0002 and $AC = $16 are the crash, measured at NOTES 072.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function s(v) if v > 32767 then return v - 65536 end return v end
local n = 0
print("f,px,py,pspd,p10,pAC,pA0," ..
      "o0x,o0y,o0z,o1x,o1y,o1z,o2x,o2y,o2z,o3x,o3y,o3z")
emu.register_frame_done(function()
  n = n + 1
  local m = mem:read_u8(0x7E0036) // 2
  if m ~= 6 and m ~= 1 then return end
  local o = {}
  for _, base in ipairs({0x1800, 0x1840, 0x1880, 0x18C0}) do
    o[#o+1] = w(base + 0x18)
    o[#o+1] = w(base + 0x1C)
    o[#o+1] = s(w(base + 0x1F))
  end
  print(string.format("%d,%d,%d,%d,%04X,%02X,%04X,%s",
    n, w(0x1018), w(0x101C), s(w(0x10EA)), w(0x1010),
    mem:read_u8(0x7E10AC), w(0x10A0), table.concat(o, ",")))
end)
