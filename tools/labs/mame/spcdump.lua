-- Snapshot the sound CPU at frame SPC_FRAME (env) of a replayed session
-- into an .spc file (SPC700 RAM + DSP registers + CPU state), which
-- ffmpeg/libgme renders to WAV: the game's own driver plays on from that
-- moment, so the snapshot IS the music that was playing.
local want = {}
for tok in (os.getenv("SPC_FRAME") or "3000"):gmatch("[^,]+") do want[tonumber(tok)] = true end
local outbase = os.getenv("SPC_OUT") or "tmp/snap"
local left = 0
for _ in pairs(want) do left = left + 1 end
local cpu = manager.machine.devices[":soundcpu"]
local ram = cpu.spaces["program"]
local dsp = manager.machine.devices[":s_dsp"].spaces["data"]
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if not want[n] then return end
  local out = outbase .. "_" .. n .. ".spc"
  local f = io.open(out, "wb")
  local hdr = "SNES-SPC700 Sound File Data v0.30" .. string.char(26, 26, 26) .. string.char(30)  -- has ID666 tag
  f:write(hdr)
  local st = cpu.state
  local names = {}
  for k, _ in pairs(st) do names[#names+1] = k end
  print("state names: " .. table.concat(names, " "))
  local function g(...) for _, k in ipairs({...}) do if st[k] then return st[k].value end end return 0 end
  local pc, a, x, y, psw, sp = g("PC", "CURPC"), g("A"), g("X"), g("Y"), g("P", "PSW", "FLAGS"), g("SP", "S")
  f:write(string.char(pc & 0xFF, (pc >> 8) & 0xFF, a & 0xFF, x & 0xFF, y & 0xFF, psw & 0xFF, sp & 0xFF, 0, 0))   -- $25..$2D
  f:write(string.rep("\0", 0x100 - 0x2E))           -- ID666 fields, blank
  local t = {}
  for i = 0, 65535 do t[#t+1] = string.char(ram:read_u8(i)) end
  -- $F1, the timer control, is write-only: a read gives 0 and the driver's
  -- loop then never ticks (a silent .spc).  Timer 0 enabled is what makes
  -- every snapshot play; 7 (all three) plays identically.
  t[0xF1 + 1] = string.char(1)
  f:write(table.concat(t))
  local d = {}
  local saved = ram:read_u8(0xF2)
  for i = 0, 127 do ram:write_u8(0xF2, i); d[#d+1] = string.char(ram:read_u8(0xF3)) end
  ram:write_u8(0xF2, saved)
  f:write(table.concat(d))
  f:write(string.rep("\0", 0x40))                    -- unused
  f:write(string.rep("\0", 0x40))                    -- extra RAM (IPL area) - zeros
  f:close()
  print("SPC written: " .. out .. " at frame " .. n)
  left = left - 1
  if left == 0 then manager.machine:exit() end
end)
