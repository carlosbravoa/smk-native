-- Capture the object-hits-wall sound from the EVENT, not from a poke.
--
-- $80:FAC7 is the wall response: it stores the surface class in $68,x,
-- calls $80:FBBC for the sound and then sets $5C,x = 8 (the port's own
-- bounce window).  With every tile forced to a wall class, objects hit
-- walls constantly, so tapping the sound entry and then logging the DSP
-- for the next few frames gives the sound as the game really keys it.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local spc = manager.machine.devices[":soundcpu"].spaces["program"]
local function dsp(r)
  local s = spc:read_u8(0xF2); spc:write_u8(0xF2, r)
  local v = spc:read_u8(0xF3); spc:write_u8(0xF2, s); return v
end
local CLS  = tonumber(os.getenv("SURF") or "80", 16)
local WANT = tonumber(os.getenv("WANT") or "30", 16)
local FROM = tonumber(os.getenv("FROM") or "2800")
local n, watch = 0, 0
local taps = {}
taps[#taps+1] = mem:install_read_tap(0x81F57A, 0x81F57A, "sfx", function()
  local a = (cpu.state["A"] and cpu.state["A"].value or 0) & 0xFF
  print(string.format("S %d %02X", n, a))
  if a == WANT and watch == 0 then watch = 10 end
end)
for _, addr in ipairs({0xF5A7, 0xF5C2, 0xF504, 0xF5F8, 0xF5E2}) do
  taps[#taps+1] = mem:install_read_tap(0x810000+addr, 0x810000+addr, "sfx", function()
    local a = (cpu.state["A"] and cpu.state["A"].value or 0) & 0xFF
    print(string.format("S %d %02X", n, a))
    if a == WANT and watch == 0 then watch = 10 end
  end)
end
_G.__sfx_taps = taps
emu.register_frame_done(function()
  n = n + 1
  if n < FROM then return end
  if n > FROM + 900 then manager.machine:exit(); return end
  for a = 0, 255 do mem:write_u8(0x7E0B00 + a, CLS) end
  if watch > 0 then
    watch = watch - 1
    local out = string.format("W %d:", n)
    for v = 0, 7 do
      local b = v * 16
      out = out .. string.format(" %d[s%02X p%04X e%d]", v, dsp(b+4),
        dsp(b+3) * 256 + dsp(b+2), dsp(b+8))
    end
    print(out)
  end
end)
