-- The start: grid placement, countdown, and the launch.
--
-- Logs every race start in a recording.  Per frame it writes the kart
-- fields that matter, and every 4 frames it dumps low WRAM so the
-- countdown timer and the rev accumulator can be FOUND by diffing rather
-- than guessed at (S2 / S11 / S17, NOTES 142/142a/142b).
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local csv = io.open(os.getenv("STARTLOG") or "startlog.csv", "w")
local bin = io.open((os.getenv("STARTLOG") or "startlog") .. ".wram", "wb")

local F = {0x18,0x1C,0x22,0x24,0x10,0x1F,0x26,0x2A,0xA0,0xA2,0xA4,0xA6,0xA8,
           0xAC,0xAE,0xB2,0xC0,0xC4,0xC8,0xCA,0xD6,0xDA,0xE2,0xE8,0xEA,0xEE,0xFA}
local hdr = {"race","frame","kart"}
for _, f in ipairs(F) do hdr[#hdr+1] = string.format("f%02X", f) end
csv:write(table.concat(hdr, ",") .. "\n")

local race, inrace, t = 0, false, 0
emu.register_frame_done(function()
  local mode = mem:read_u8(0x7E0036)
  if mode == 2 and not inrace then
    inrace = true; race = race + 1; t = 0
    print(string.format("RACE %d starts, track $0124 = %d", race, mem:read_u16(0x7E0124)))
  elseif mode ~= 2 and inrace then
    inrace = false
    print(string.format("RACE %d ended after %d frames", race, t))
  end
  if not inrace or t > 400 then if inrace then t = t + 1 end return end
  for _, base in ipairs({0x1000, 0x1100}) do
    local row = {tostring(race), tostring(t), string.format("%x", base)}
    for _, f in ipairs(F) do
      row[#row+1] = string.format("%d", mem:read_u16(0x7E0000 + base + f))
    end
    csv:write(table.concat(row, ",") .. "\n")
  end
  if t % 4 == 0 then                    -- low WRAM, for diffing
    local s = {}
    for a = 0x0000, 0x0FFF, 2 do s[#s+1] = string.pack("<I2", mem:read_u16(0x7E0000 + a)) end
    bin:write(string.pack("<I2I2", race, t) .. table.concat(s))
  end
  t = t + 1
end)
emu.register_stop(function() csv:close(); bin:close() end)
