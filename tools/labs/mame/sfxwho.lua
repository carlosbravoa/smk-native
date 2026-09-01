-- WHO plays each sound.  A read tap on the opcode fetch at $81:F57A -
-- the play-sound entry - fires as the CPU arrives there, and the JSL's
-- return address is sitting on the stack, so every request in a real
-- recorded race can be attributed to the ROUTINE that asked for it.
-- That turns naming sounds from an ear exercise into a lookup: the
-- routines are already decoded in docs/NOTES.md.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local n = 0
local seen = {}
emu.register_frame_done(function() n = n + 1 end)
local function caller_of(depth)
  local st = cpu.state
  local s = st["S"] and st["S"].value or 0
  -- JSL pushed PBR, PCH, PCL: at entry S+1 = PCL, S+2 = PCH, S+3 = PBR.
  -- depth 1 steps over an inner JSR's two bytes first, which is how the
  -- COMPUTED requests are attributed: they come through $81:F5E2 from a
  -- JSR inside bank $81, and the caller that matters is the JSL below it.
  -- via $81:F5A7 / $81:F5C2 the stack at $F5E2 holds the JSR's two
  -- bytes, then PHP, PHY, PHX - so the outer JSL's return is seven
  -- bytes down.  That is how a COMPUTED id gets attributed.
  local base = s + (depth == 1 and 7 or 0)
  local lo = mem:read_u8(base + 1)
  local hi = mem:read_u8(base + 2)
  local bk = mem:read_u8(base + 3)
  local pc = (hi << 8 | lo) - 3
  return bk, pc & 0xFFFF
end
-- the handles MUST be kept: a tap whose handle is collected stops
-- firing, which looked exactly like "the game never calls this"
local taps = {}
for _, addr in ipairs({0xF57A, 0xF5A7, 0xF5C2, 0xF504, 0xF5F8, 0xF5E2}) do
  taps[#taps+1] = mem:install_read_tap(0x810000 + addr, 0x810000 + addr, "sfx", function(off, data, mask)
    local a = cpu.state["A"] and cpu.state["A"].value or 0
    local bk, pc = caller_of(addr == 0xF5E2 and 1 or 0)
    local key = string.format("%04X|%02X:%04X|%04X", a & 0xFFFF, bk, pc, addr)
    seen[key] = (seen[key] or 0) + 1
    print(string.format("%d,%04X,%02X:%04X,F%03X", n, a & 0xFFFF, bk, pc, addr))
  end)
end
_G.__sfx_taps = taps
