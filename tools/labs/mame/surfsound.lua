-- Force the ground under every kart and listen to what changes.
--
-- RAM $0B00 is the tilemap-byte -> surface-class table ($81:EB11 fills
-- it, NOTES 011).  Overwriting every entry makes the WHOLE course one
-- class, so the same recorded inputs can be run over road and over
-- grass and the two sound pictures subtracted.  SURF=5A is grass,
-- SURF=40 the road control.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local spc = manager.machine.devices[":soundcpu"].spaces["program"]
local CLS = tonumber(os.getenv("SURF") or "40", 16)
local FROM = tonumber(os.getenv("FROM") or "1200")
local function dsp(r)
  local s = spc:read_u8(0xF2); spc:write_u8(0xF2, r)
  local v = spc:read_u8(0xF3); spc:write_u8(0xF2, s); return v
end
local cpu = manager.machine.devices[":maincpu"]
local taps = {}
local n = 0
for _, addr in ipairs({0xF57A, 0xF5A7, 0xF5C2, 0xF504, 0xF5F8, 0xF5E2}) do
  taps[#taps+1] = mem:install_read_tap(0x810000+addr, 0x810000+addr, "sfx", function()
    local a = cpu.state["A"] and cpu.state["A"].value or 0
    print(string.format("S %d %02X", n, a & 0xFF))
  end)
end
_G.__sfx_taps = taps
emu.register_frame_done(function()
  n = n + 1
  if n >= FROM then
    for i = 0, 255 do mem:write_u8(0x7E0B00 + i, CLS) end
  end
  if n >= FROM + 60 and n % 5 == 0 and n < FROM + 900 then
    local out = string.format("V %d surf %02X:", n, mem:read_u8(0x7E1068))
    for v = 0, 7 do
      local b = v * 16
      out = out .. string.format(" %d[s%02X p%04X e%d]", v, dsp(b+4),
        dsp(b+3) * 256 + dsp(b+2), dsp(b+8))
    end
    print(out)
  end
  if n > FROM + 900 then manager.machine:exit() end
end)
