-- Does an AI kart make a noise when it lets go of what it is carrying?
--
-- $6C,x on an object block is the CARRY counter ($80:F3B6 decrements it
-- and drags the object to its owner while it runs); $80:F442 plays the
-- throw through $84:D955 on the frame it reaches 1.  That code is the
-- OBJECT's, not the player's - so an AI's release should sound too.
-- This watches both halves at once in a real recording: every object's
-- carry and owner, and every sound request with its caller.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local n = 0
local function caller_of(depth)
  local s = cpu.state["S"] and cpu.state["S"].value or 0
  local base = s + (depth == 1 and 7 or 0)
  local lo, hi, bk = mem:read_u8(base+1), mem:read_u8(base+2), mem:read_u8(base+3)
  return bk, ((hi << 8 | lo) - 3) & 0xFFFF
end
local taps = {}
for _, addr in ipairs({0xF57A, 0xF5A7, 0xF5C2, 0xF504, 0xF5F8, 0xF5E2}) do
  taps[#taps+1] = mem:install_read_tap(0x810000+addr, 0x810000+addr, "sfx", function()
    local a = cpu.state["A"] and cpu.state["A"].value or 0
    local bk, pc = caller_of(addr == 0xF5E2 and 1 or 0)
    print(string.format("S %d %02X %02X:%04X", n, a & 0xFF, bk, pc))
  end)
end
_G.__sfx_taps = taps

local BL = {0x1800, 0x1880, 0x1900, 0x1980, 0x1A00, 0x1A80}
local function w(a) return mem:read_u8(0x7E0000+a) | (mem:read_u8(0x7E0000+a+1) << 8) end
local prev = {}
for _, b in ipairs(BL) do prev[b] = 0 end
emu.register_frame_done(function()
  n = n + 1
  for _, b in ipairs(BL) do
    local c = w(b + 0x6C)
    if c ~= prev[b] then
      if prev[b] > 0 and c == 0 then
        print(string.format("D %d %04X RELEASED owner $%04X", n, b, w(b + 0x6A)))
      elseif prev[b] == 0 and c > 0 then
        print(string.format("D %d %04X CARRY %d owner $%04X", n, b, c, w(b + 0x6A)))
      end
      prev[b] = c
    end
  end
end)
