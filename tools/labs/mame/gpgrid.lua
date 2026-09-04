-- The grid of a cup's second race on: where does it come from?
--
-- Per race in a recording: the grid at the start (block, slot by y, the
-- rank word $E6 and the $010E order table), the order table and every
-- kart's $C0 progress at the end, and - between races - every frame on
-- which $010E..$011C or any kart's $E6 changes, with the PC-less "what
-- changed" so the writer can then be trapped.  Low WRAM ($0000-$1FFF) is
-- dumped at every mode change so the cup's points storage can be found by
-- diffing (GPGRID=prefix names the dump files).
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local prefix = os.getenv("GPGRID") or "gpgrid"
local poke_at = tonumber(os.getenv("POKE_AT") or "0")
local frames, race, inrace, lastmode = 0, 0, false, -1
local prev_tab, prev_e6 = {}, {}

local function rd16(a) return mem:read_u16(0x7E0000 + a) end

local function order_table()
  local t = {}
  for i = 0, 7 do t[i] = rd16(0x010E + i * 2) end
  return t
end

local function tab_str(t)
  local s = {}
  for i = 0, 7 do s[#s+1] = string.format("%04X", t[i]) end
  return table.concat(s, " ")
end

local function karts()
  local s = {}
  for k = 0, 7 do
    local b = 0x1000 + k * 0x100
    s[#s+1] = string.format("  k%d x %4d y %4d E6 %04X C0 %04X F2 %04X",
      k, rd16(b + 0x18), rd16(b + 0x1C), rd16(b + 0xE6), rd16(b + 0xC0), rd16(b + 0xF2))
  end
  return table.concat(s, "\n")
end

local function dump(tag)
  local f = io.open(string.format("%s_%s_f%d.wram", prefix, tag, frames), "wb")
  local s = {}
  for a = 0x0000, 0x1FFF, 2 do s[#s+1] = string.pack("<I2", rd16(a)) end
  f:write(table.concat(s)); f:close()
end

emu.register_frame_done(function()
  frames = frames + 1
  local mode = rd16(0x0036)
  if mode ~= lastmode then
    print(string.format("f%d mode $36 %d -> %d   $0124 %d cup %d course %d  $010E: %s",
      frames, lastmode, mode, rd16(0x0124), rd16(0x0150), rd16(0x0152), tab_str(order_table())))
    dump(string.format("mode%d", mode))
    local pts = {}
    for k = 0, 7 do pts[#pts+1] = string.format("k%d=%d", k, rd16(0x10F0 + k * 0x100)) end
    print("   points $F0: " .. table.concat(pts, " "))
    lastmode = mode
  end
  if mode == 2 and not inrace then
    inrace = true; race = race + 1
    print(string.format("RACE %d START f%d track %d", race, frames, rd16(0x0124)))
    print(karts())
  elseif mode ~= 2 and inrace then
    inrace = false
    print(string.format("RACE %d END f%d   $010E: %s", race, frames, tab_str(order_table())))
    print(karts())
  end
  if poke_at > 0 and frames == poke_at then
    -- swap ranks 1 and 2: the second kart takes the lead in the table
    local i, j = tonumber(os.getenv("SWAP_I") or "0"), tonumber(os.getenv("SWAP_J") or "1")
    local a, b = rd16(0x010E + i * 2), rd16(0x010E + j * 2)
    mem:write_u16(0x7E010E + i * 2, b); mem:write_u16(0x7E010E + j * 2, a)
    local ea, eb = mem:read_u16(0x7E0000 + a + 0xE6), mem:read_u16(0x7E0000 + b + 0xE6)
    mem:write_u16(0x7E0000 + a + 0xE6, eb); mem:write_u16(0x7E0000 + b + 0xE6, ea)
    print(string.format("f%d POKED: $010E %04X<->%04X, their E6 %04X<->%04X", frames, a, b, ea, eb))
  end
  local rank_at = tonumber(os.getenv("RANKOUT_AT") or "0")
  if rank_at > 0 and frames == rank_at then
    -- four AI karts are declared FINISHED ahead of the player: $C0 to the
    -- final lap's threshold, so the player crosses fifth
    for _, k in ipairs({7, 6, 5, 4}) do
      local b = 0x7E1000 + k * 0x100
      local c0 = mem:read_u16(b + 0xC0)
      mem:write_u16(b + 0xC0, 0x8500 | (c0 & 0xFF))
    end
    print(string.format("f%d RANKOUT: karts 7 6 5 4 poked to lap $85", frames))
  end
  if not inrace then
    local t = order_table()
    local changed = false
    for i = 0, 7 do if t[i] ~= prev_tab[i] then changed = true end end
    if changed then
      print(string.format("f%d  $010E: %s", frames, tab_str(t)))
      prev_tab = t
    end
    for k = 0, 7 do
      local e = rd16(0x1000 + k * 0x100 + 0xE6)
      if e ~= prev_e6[k] then
        print(string.format("f%d  k%d E6 %04X -> %04X", frames, k, prev_e6[k] or -1, e))
        prev_e6[k] = e
      end
    end
  end
end)
