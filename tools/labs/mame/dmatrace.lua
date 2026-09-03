-- WHERE DOES VRAM COME FROM?  Every VRAM-bound DMA, with its source.
--
-- Byte-searching the ROM for tiles assumes the art is stored in the same
-- form VRAM holds it, and that assumption is what left five sprite sets
-- "not in the ROM" (NOTES 269/270).  This does not assume: it taps the
-- DMA enable ($420B), reads each enabled channel's own registers and
-- reports source bank:address -> VRAM word address, size.
--
-- A ROM source is the art in place.  A WRAM source ($7E/$7F) means it was
-- decompressed there first, and the next question is who wrote it.
--
--   VDEST=0x5890   only report transfers landing on that VRAM word
--   VNEAR=0x40     ...within this many words (default 0x200)
--   FROM=frame     ignore everything before this frame
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local want  = tonumber(os.getenv("VDEST") or "-1")
local near  = tonumber(os.getenv("VNEAR") or "512")
local from  = tonumber(os.getenv("FROM") or "0")
local n, vaddr = 0, 0
local seen = {}

-- $2116/$2117 set the VRAM word address the next writes land on
local taps = {}
taps[#taps+1] = mem:install_write_tap(0x002116, 0x002117, "vaddr", function(off, data)
  if (off & 1) == 0 then vaddr = (vaddr & 0xFF00) | (data & 0xFF)
  else                   vaddr = (vaddr & 0x00FF) | ((data & 0xFF) << 8) end
end)

taps[#taps+1] = mem:install_write_tap(0x00420B, 0x00420B, "mdmaen", function(off, data)
  if n < from then return end
  for ch = 0, 7 do
    if (data >> ch) & 1 == 1 then
      local b = 0x4300 + ch * 0x10
      local bbus = mem:read_u8(b + 1)
      -- $18/$19 are VMDATAL/H: this transfer is going into VRAM
      if bbus == 0x18 or bbus == 0x19 then
        local src = mem:read_u8(b + 2) | (mem:read_u8(b + 3) << 8)
        local bank = mem:read_u8(b + 4)
        local len  = mem:read_u8(b + 5) | (mem:read_u8(b + 6) << 8)
        if len == 0 then len = 0x10000 end
        if want < 0 or (vaddr >= want - near and vaddr <= want + near) then
          local key = string.format("%02X:%04X->%04X:%d", bank, src, vaddr, len)
          if not seen[key] then
            seen[key] = n
            print(string.format("D f%-6d src $%02X:%04X  %5d bytes -> VRAM word $%04X (tile $%03X)",
              n, bank, src, len, vaddr, vaddr // 16))
          end
        end
      end
    end
  end
end)
_G.__dma_taps = taps

emu.register_frame_done(function() n = n + 1 end)
