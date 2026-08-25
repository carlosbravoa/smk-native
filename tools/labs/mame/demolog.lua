-- Log both attract-race karts' physics fields per frame, and catch the
-- setup writers of the $0710 per-player block and $B4,x.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local out = io.open(os.getenv("DEMOLOG") or "demolog.csv", "w")
local FIELDS = {0x16,0x1A,0x22,0x24,0xAE,0x10,0x12,0x18,0x1C,0x1F,0x26,0x28,0x2A,0x60,0xA0,0xA2,0xA4,0xA6,0xA8,0xAA,0xAC,0xAE,0xB0,0xB2,0xB4,0xC2,0xC4,0xCA,0xD6,0xDE,0xE0,0xE2,0xE4,0xEA,0xFA,0xFC,0xE8,0xEE,0xB8}
local hdr = {"frame","kart","g28","g2A","g2C","g2E","g30","gE00","gE02","g124","g126"}
for _, f in ipairs(FIELDS) do hdr[#hdr+1] = string.format("f%02X", f) end
out:write(table.concat(hdr, ",") .. "\n")
local writers, taps = {}, {}
local function tap(lo, hi, name)
  for _, bank in ipairs({0x00, 0x7E}) do
    taps[#taps+1] = mem:install_write_tap((bank<<16)|lo, (bank<<16)|hi, name..bank, function(off, data, mask)
      local k = string.format("%s %04x from %06x", name, off & 0xFFFF, cpu.state["PC"].value)
      writers[k] = (writers[k] or 0) + 1 end)
  end
end
tap(0x0710, 0x07BF, "blk")
tap(0x10B4, 0x10B5, "B4")
tap(0x1012, 0x1013, "12")
local n, logged, started = 0, 0, false

emu.register_frame_done(function()
  n = n + 1
  local mode = mem:read_u8(0x7E0036)
  if mode == 2 and not started then
    started = true
    local t = {}
    for a = 0x0710, 0x07BF, 2 do t[#t+1] = string.format("%04x", mem:read_u16(0x7E0000 + a)) end
    print("BLOCK0710 " .. table.concat(t, " "))
    for k, v in pairs(writers) do print("WRITER", k, v) end
  end
  if started then
    if mode ~= 2 then print("race ended at frame", n); manager.machine:exit() end
    for _, base in ipairs({0x1000, 0x1100}) do
      local row = {tostring(logged), string.format("%x", base), tostring(mem:read_u16(0x7E0028)), tostring(mem:read_u16(0x7E002A)), tostring(mem:read_u16(0x7E002C)), tostring(mem:read_u16(0x7E002E)), tostring(mem:read_u16(0x7E0030)), tostring(mem:read_u16(0x7E0E00)), tostring(mem:read_u16(0x7E0E02)), tostring(mem:read_u16(0x7E0124)), tostring(mem:read_u16(0x7E0126))}
      for _, f in ipairs(FIELDS) do row[#row+1] = string.format("%d", mem:read_u16(0x7E0000 + base + f)) end
      out:write(table.concat(row, ",") .. "\n")
    end
    logged = logged + 1
    if logged >= 6000 then out:close(); manager.machine:exit() end
  end
end)
