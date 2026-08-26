local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local V = {}                       -- byte shadow of VRAM
local addr, inc, mode = 0, 1, 0x80
local cg, cgaddr = {}, 0
local taps = {}
local banks = {} for b = 0, 0x3F do banks[#banks+1] = b end for b = 0x80, 0xBF do banks[#banks+1] = b end
local function vwrite(off, val)
    V[off % 0x10000] = val
end
local function step() addr = (addr + inc) % 0x8000 end
local function portwrite(which, d)
  if which == 0x18 then
    vwrite(addr*2, d)
    if (mode & 0x80) == 0 then step() end
  else
    vwrite(addr*2+1, d)
    if (mode & 0x80) ~= 0 then step() end
  end
end
for _, b in ipairs(banks) do
  local B = b << 16
  taps[#taps+1] = mem:install_write_tap(B|0x2115, B|0x2115, "vmain", function(o, d) mode = d
      local s = d & 3; inc = (s == 0) and 1 or (s == 1) and 32 or 128 end)
  taps[#taps+1] = mem:install_write_tap(B|0x2116, B|0x2116, "vlo", function(o, d) addr = (addr & 0xFF00) | (d & 0xFF) end)
  taps[#taps+1] = mem:install_write_tap(B|0x2117, B|0x2117, "vhi", function(o, d) addr = (addr & 0x00FF) | ((d & 0xFF) << 8) end)
  taps[#taps+1] = mem:install_write_tap(B|0x2118, B|0x2118, "vdl", function(o, d) portwrite(0x18, d & 0xFF) end)
  taps[#taps+1] = mem:install_write_tap(B|0x2119, B|0x2119, "vdh", function(o, d) portwrite(0x19, d & 0xFF) end)
  taps[#taps+1] = mem:install_write_tap(B|0x2121, B|0x2121, "cga", function(o, d) cgaddr = (d & 0xFF) * 2 end)
  taps[#taps+1] = mem:install_write_tap(B|0x2122, B|0x2122, "cgd", function(o, d) cg[cgaddr % 512] = d & 0xFF; cgaddr = cgaddr + 1 end)
  taps[#taps+1] = mem:install_write_tap(B|0x420B, B|0x420B, "dma", function(o, d)
    for ch = 0, 7 do
      if (d >> ch) & 1 == 1 then
        local r = 0x004300 | (ch << 4)
        local dmap = mem:read_u8(r); local bb = mem:read_u8(r+1)
        local a = mem:read_u8(r+2) | (mem:read_u8(r+3)<<8); local ab = mem:read_u8(r+4)
        local sz = mem:read_u8(r+5) | (mem:read_u8(r+6)<<8); if sz == 0 then sz = 0x10000 end
        local fixed = (dmap & 0x08) ~= 0
        if bb == 0x18 or bb == 0x19 then
          local unit = dmap & 7
          for i = 0, sz - 1 do
            local byte = mem:read_u8((ab << 16) | ((a + (fixed and 0 or i)) & 0xFFFF))
            local which = bb
            if unit == 1 then which = (i % 2 == 0) and 0x18 or 0x19 end
            portwrite(which, byte)
          end
        elseif bb == 0x22 then
          for i = 0, sz - 1 do
            cg[cgaddr % 512] = mem:read_u8((ab << 16) | ((a + (fixed and 0 or i)) & 0xFFFF)); cgaddr = cgaddr + 1
          end
        end
      end
    end end)
end
local n, shot = 0, 0
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) == 2 then
    shot = shot + 1
    if shot == 900 then
      local f = io.open(os.getenv("VOUT"), "wb")
      for i = 0, 0xFFFF do f:write(string.char(V[i] or 0)) end
      f:close()
      local c = io.open(os.getenv("VOUT") .. ".cg", "wb")
      for i = 0, 511 do c:write(string.char(cg[i] or 0)) end
      c:close()
      print("dumped")
      manager.machine:exit()
    end
  end
  if n > 20000 then manager.machine:exit() end
end)
