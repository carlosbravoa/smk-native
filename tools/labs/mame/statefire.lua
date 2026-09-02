-- Press UP + the item button in one of the user's savestates.
--
-- UP is what was missing: the item button ALONE was giving one action,
-- and the user set up two states to show the other one - "press up and
-- simultaneously the item button".  The pad READ at $80:8445 is patched
-- for two frames (LDA #imm), which gives a clean edge through the game's
-- own EOR/AND pressed-word logic.  PAD=0880 is UP+A.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local spc = manager.machine.devices[":soundcpu"].spaces["program"]
local rom = manager.machine.memory.regions[":snsslot:cart:rom"]
local function dsp(r)
  local s = spc:read_u8(0xF2); spc:write_u8(0xF2, r)
  local v = spc:read_u8(0xF3); spc:write_u8(0xF2, s); return v
end
local PAD    = tonumber(os.getenv("PAD") or "0880", 16)
local FIRE   = tonumber(os.getenv("FIRE") or "90")
local SPAN   = tonumber(os.getenv("SPAN") or "150")
local NOFIRE = os.getenv("NOFIRE") ~= nil
local PAD_READ = 0x8445
local function pad_force(on)
  if on then
    rom:write_u8(PAD_READ, 0xA9)
    rom:write_u8(PAD_READ+1, PAD & 0xFF); rom:write_u8(PAD_READ+2, (PAD >> 8) & 0xFF)
  else
    rom:write_u8(PAD_READ, 0xBD); rom:write_u8(PAD_READ+1, 0x18); rom:write_u8(PAD_READ+2, 0x42)
  end
end
-- MAME cancels the autoboot script if -state is given on the command
-- line, so the state is loaded from here instead and everything is timed
-- from the load.
local SLOT = os.getenv("SMK_STATE") or "1"
local LOAD = tonumber(os.getenv("LOAD") or "10")
local n = 0
local taps = {}
local function caller_of(depth)
  local s = cpu.state["S"] and cpu.state["S"].value or 0
  local base = s + (depth == 1 and 7 or 0)
  local lo, hi, bk = mem:read_u8(base+1), mem:read_u8(base+2), mem:read_u8(base+3)
  return bk, ((hi << 8 | lo) - 3) & 0xFFFF
end
for _, addr in ipairs({0xF57A, 0xF5A7, 0xF5C2, 0xF504, 0xF5F8, 0xF5E2}) do
  taps[#taps+1] = mem:install_read_tap(0x810000+addr, 0x810000+addr, "sfx", function()
    local a = cpu.state["A"] and cpu.state["A"].value or 0
    local bk, pc = caller_of(addr == 0xF5E2 and 1 or 0)
    print(string.format("S %d %02X %02X:%04X", n, a & 0xFF, bk, pc))
  end)
end
_G.__sfx_taps = taps
local function w(a) return mem:read_u8(0x7E0000+a) | (mem:read_u8(0x7E0000+a+1) << 8) end
emu.register_frame_done(function()
  n = n + 1
  if n == LOAD then manager.machine:load(SLOT) end
  if n < LOAD + 2 then return end
  if not NOFIRE then
    if n == FIRE - 1 then pad_force(true) end
    if n == FIRE + 1 then pad_force(false) end
  end
  if n >= FIRE - 5 and n <= FIRE + SPAN then
    print(string.format("P %d item %04X held %04X pressed %04X", n,
      w(0x0D70), w(0x0020), w(0x0028)))
    local out = string.format("V %d:", n)
    for v = 0, 7 do
      local b = v * 16
      out = out .. string.format(" %d[s%02X p%04X e%d]", v, dsp(b+4),
        dsp(b+3) * 256 + dsp(b+2), dsp(b+8))
    end
    print(out)
  end
  if n > FIRE + SPAN then manager.machine:exit() end
end)
