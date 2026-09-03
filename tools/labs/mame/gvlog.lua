-- A whole recorded run, logged for five questions at once (NOTES 265):
-- speed and lap timing, the engine rev against speed, the surface class
-- under the kart, and every sound the game asks for WITH ITS CALLER, so a
-- sound can be attributed to the code that made it rather than to a guess.
--
--   P <frame> <speed> <x> <y> <surf $AE> <rev $42> <lap $C0> <state $A6> <z $1F> <air>
--   S <frame> <id> <bank:pc>
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local P1 = 0x1000
local n = 0
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function b(a) return mem:read_u8(0x7E0000 + a) end

-- every sound request, with the caller's PC (aidropsfx.lua's trick)
local function caller_of(depth)
  local s = cpu.state["S"] and cpu.state["S"].value or 0
  local base = s + (depth == 1 and 7 or 0)
  local lo, hi, bk = mem:read_u8(base+1), mem:read_u8(base+2), mem:read_u8(base+3)
  return bk, ((hi << 8 | lo) - 3) & 0xFFFF
end
local taps = {}
for _, addr in ipairs({0xF57A, 0xF5A7, 0xF5C2}) do
  taps[#taps+1] = mem:install_read_tap(0x810000+addr, 0x810000+addr, "sfx", function()
    local a = cpu.state["A"] and cpu.state["A"].value or 0
    local bk, pc = caller_of(0)
    print(string.format("S %d %02X %02X:%04X", n, a & 0xFF, bk, pc))
  end)
end
_G.__gv_taps = taps

emu.register_frame_done(function()
  n = n + 1
  local spd = w(P1 + 0xEA)
  if spd > 32767 then spd = spd - 65536 end
  print(string.format("P %d %d %d %d %02X %d %04X %02X %d %d",
    n, spd, w(P1 + 0x18), w(P1 + 0x1C), b(P1 + 0xAE), w(P1 + 0x42),
    w(P1 + 0xC0), b(P1 + 0xA6), w(P1 + 0x1F), (w(P1 + 0xE2) & 0x8000) ~= 0 and 1 or 0))
end)
