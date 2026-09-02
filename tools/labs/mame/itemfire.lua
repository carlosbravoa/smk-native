-- Hand the player an item and press the button, in a real recording.
--
-- The item word is poked ($0D70 = $C000|id, the oracle's own trick) and
-- the A button is forced by TAPPING the joypad register $4218 and
-- returning it with bit 7 set - the recorded inputs stay untouched
-- otherwise.  Then every DSP voice is logged per frame around the throw,
-- because the sounds that matter here are not queued at all: a pitch
-- that FALLS is a voice, not a request.
--
--   ITEM=03 FIRE=3000 FROM=2900 [NOFIRE=1]
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local spc = manager.machine.devices[":soundcpu"].spaces["program"]
local function dsp(r)
  local s = spc:read_u8(0xF2); spc:write_u8(0xF2, r)
  local v = spc:read_u8(0xF3); spc:write_u8(0xF2, s); return v
end
local ITEM   = tonumber(os.getenv("ITEM") or "03", 16)
local FIRE   = tonumber(os.getenv("FIRE") or "3000")
local FROM   = tonumber(os.getenv("FROM") or (FIRE - 100))
local NOFIRE = os.getenv("NOFIRE") ~= nil
local SPAN   = tonumber(os.getenv("SPAN") or "150")
local n = 0
local taps = {}

-- Force A by patching the pad READ for two frames.  $80:8445 is
-- `LDA $4218,x / STA $20,x` - held - and three instructions later
-- `EOR $24,x / AND $20,x / STA $28,x` makes the newly-PRESSED word, so a
-- two-frame patch gives a clean edge where a tap on $4218 gave nothing.
-- BD 18 42 (LDA $4218,x) and A9 80 00 (LDA #$0080, A alone) are both
-- three bytes, so the patch goes in and comes out in place.
local rom = manager.machine.memory.regions[":snsslot:cart:rom"]
local PAD_READ = 0x8445
local function pad_force(on)
  if on then
    rom:write_u8(PAD_READ, 0xA9); rom:write_u8(PAD_READ+1, 0x80); rom:write_u8(PAD_READ+2, 0x00)
  else
    rom:write_u8(PAD_READ, 0xBD); rom:write_u8(PAD_READ+1, 0x18); rom:write_u8(PAD_READ+2, 0x42)
  end
end
-- and every sound REQUEST, with its caller, for the ones that are queued
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
  if n >= FROM and n < FIRE then
    mem:write_u8(0x7E0D70, ITEM); mem:write_u8(0x7E0D71, 0xC0)   -- $C000|id, ready
  end
  if not NOFIRE then
    if n == FIRE - 1 then pad_force(true) end
    if n == FIRE + 1 then pad_force(false) end
  end
  if n >= FIRE - 5 and n <= FIRE + SPAN then
    print(string.format("P %d item %04X held %04X pressed %04X prev %04X state %04X",
      n, w(0x0D70), w(0x0020), w(0x0028), w(0x0024), w(0x10A6)))
    local out = string.format("V %d item %04X:", n, w(0x0D70))
    for v = 0, 7 do
      local b = v * 16
      out = out .. string.format(" %d[s%02X p%04X e%d]", v, dsp(b+4),
        dsp(b+3) * 256 + dsp(b+2), dsp(b+8))
    end
    print(out)
    local o = ""
    for _, b in ipairs({0x1800, 0x1880, 0x1900, 0x1980, 0x1A00, 0x1A80}) do
      if w(b + 0x12) & 0x8000 ~= 0 then
        o = o .. string.format(" %04X[a%04X s%d c%d]", b, w(b+0x2A), w(b+0x72), w(b+0x6C))
      end
    end
    if o ~= "" then print("O " .. n .. o) end
  end
  if n > FIRE + SPAN then manager.machine:exit() end
end)
