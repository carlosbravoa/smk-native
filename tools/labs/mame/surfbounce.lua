-- Two open questions in one pass over a recording (the user: "bouncing
-- shells don't sound like that" and "when driving on grass there is a
-- sound coming up, like S-S-S"):
--
--   * the player's own surface byte, $68,x ($1068 for P1, NOTES 011),
--     so every sound can be attributed to the ground under the kart;
--   * every object's velocity words $22/$24, so a shell REVERSING - a
--     bounce - is an event we can put a sound beside;
--   * plus the DSP's eight voices, because a surface hiss may well be a
--     HELD voice that is never queued at all.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local spc = manager.machine.devices[":soundcpu"].spaces["program"]
local n = 0
local function dsp(r)
  local s = spc:read_u8(0xF2); spc:write_u8(0xF2, r)
  local v = spc:read_u8(0xF3); spc:write_u8(0xF2, s); return v
end
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
    print(string.format("S %d %02X %02X:%04X surf %02X", n, a & 0xFF, bk, pc,
      mem:read_u8(0x7E1068)))
  end)
end
_G.__sfx_taps = taps

local function w(a) return mem:read_u8(0x7E0000+a) | (mem:read_u8(0x7E0000+a+1) << 8) end
local function s16(v) return v >= 0x8000 and v - 0x10000 or v end
local BL = {0x1800, 0x1880, 0x1900, 0x1980, 0x1A00, 0x1A80}
local pv = {}
for _, b in ipairs(BL) do pv[b] = {0, 0} end
local psurf = -1
emu.register_frame_done(function()
  n = n + 1
  local su = mem:read_u8(0x7E1068)
  if su ~= psurf then
    print(string.format("G %d surface $%02X", n, su))
    psurf = su
  end
  for _, b in ipairs(BL) do
    local vx, vy = s16(w(b + 0x22)), s16(w(b + 0x24))
    local ox, oy = pv[b][1], pv[b][2]
    -- a bounce: a velocity component big enough to matter, reversed
    if (ox * vx < 0 and math.abs(ox) > 0x40) or (oy * vy < 0 and math.abs(oy) > 0x40) then
      print(string.format("B %d %04X (%d,%d)->(%d,%d)", n, b, ox, oy, vx, vy))
    end
    pv[b] = {vx, vy}
  end
  -- the voice picture, once a second, to catch a HELD surface sound
  if n % 30 == 0 then
    local out = string.format("V %d surf %02X:", n, su)
    for v = 0, 7 do
      local base = v * 16
      out = out .. string.format(" %d[s%02X p%04X e%d]", v, dsp(base+4),
        dsp(base+3) * 256 + dsp(base+2), dsp(base+8))
    end
    print(out)
  end
end)
