-- Log both attract-race karts' physics fields per frame, and catch the
-- setup writers of the $0710 per-player block and $B4,x.
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local out = nil
local race = 0
local journal = io.open((os.getenv("DEMODIR") or ".") .. "/demos.txt", "w")
local FIELDS = {0x16,0x1A,0x22,0x24,0xAE,0x10,0x12,0x18,0x1C,0x1F,0x26,0x28,0x2A,0x60,0xA0,0xA2,0xA4,0xA6,0xA8,0xAA,0xAC,0xAE,0xB0,0xB2,0xB4,0xC2,0xC4,0xCA,0xD6,0xDE,0xE0,0xE2,0xE4,0xEA,0xFA,0xFC,0xE8,0xEE,0xB8}
local hdr = {"frame","kart","g28","g2A","g2C","g2E","g30","gE00","gE02","g124","g126"}
for _, f in ipairs(FIELDS) do hdr[#hdr+1] = string.format("f%02X", f) end

local n, logged, started, lastmode = 0, 0, false, -1
emu.register_frame_done(function()
  n = n + 1
  local mode = mem:read_u8(0x7E0036)
  if mode ~= lastmode then
    journal:write(string.format("frame %d mode %d track %d chars %d/%d\n", n, mode, mem:read_u16(0x7E0124), mem:read_u16(0x7E1012) // 2, mem:read_u16(0x7E1112) // 2)); journal:flush()
    lastmode = mode
  end
  if mode == 2 and not started then
    started = true; race = race + 1; logged = 0
    out = io.open(string.format("%s/demo%d_track%d.csv", os.getenv("DEMODIR") or ".", race, mem:read_u16(0x7E0124)), "w")
    out:write(table.concat(hdr, ",") .. "\n")
  end
  if started and mode ~= 2 then started = false; out:close(); out = nil
    if race >= tonumber(os.getenv("DEMOS") or "5") then journal:close(); manager.machine:exit() end
  end
  if started then
    for _, base in ipairs({0x1000, 0x1100}) do
      local row = {tostring(logged), string.format("%x", base), tostring(mem:read_u16(0x7E0028)), tostring(mem:read_u16(0x7E002A)), tostring(mem:read_u16(0x7E002C)), tostring(mem:read_u16(0x7E002E)), tostring(mem:read_u16(0x7E0030)), tostring(mem:read_u16(0x7E0E00)), tostring(mem:read_u16(0x7E0E02)), tostring(mem:read_u16(0x7E0124)), tostring(mem:read_u16(0x7E0126))}
      for _, f in ipairs(FIELDS) do row[#row+1] = string.format("%d", mem:read_u16(0x7E0000 + base + f)) end
      out:write(table.concat(row, ",") .. "\n")
    end
    logged = logged + 1
  end
  if n > 60000 then journal:close(); if out then out:close() end manager.machine:exit() end
end)
