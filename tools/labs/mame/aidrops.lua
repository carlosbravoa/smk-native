-- Every arming of the two projectile blocks ($1A00/$1A80) by an AI owner,
-- with the field's state at that frame: who, what, where the player is.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local function s(v) if v > 32767 then return v - 65536 end return v end
local n, last = 0, {}
print("f,slot,owner,variant,live,ox,oy,ohead,olap,orank,px,py,phead,plap,prank,dist,bearing_from_owner,pspd,ospd")
emu.register_frame_done(function()
  n = n + 1
  if mem:read_u8(0x7E0036) ~= 2 then return end
  for _, blk in ipairs({0x1A00, 0x1A80}) do
    local live = (w(blk + 0x12) & 0x8000) ~= 0
    local owner, var = w(blk + 0x6A), w(blk + 0x70)
    local ox, oy = w(blk + 0x18), w(blk + 0x1C)
    local key = string.format("%d,%d,%d", live and 1 or 0, owner, var)
    local ok = owner >= 0x1000 and owner <= 0x1700
    local moved = false
    if ok then
      local kx, ky = w(owner + 0x18), w(owner + 0x1C)
      moved = math.abs(kx - ox) <= 2 and math.abs(ky - oy) <= 2
    end
    local prev = last[blk]
    -- an arming: went live, or changed hands / kind, or snapped back onto its owner
    if live and (prev == nil or prev.key ~= key or (moved and not prev.moved)) then
      if ok then
        local px, py = w(0x1018), w(0x101C)
        local dx, dy = px - w(owner + 0x18), py - w(owner + 0x1C)
        local dist = math.floor(math.sqrt(dx * dx + dy * dy))
        -- bearing of the player from the owner, relative to the owner's heading (0 = dead ahead, 180 = behind)
        local ohead = w(owner + 0x2A)
        local ang = math.deg(math.atan(dx, -dy))       -- game: 0 points -Y
        local rel = (ang - ohead * 360 / 65536 + 540) % 360 - 180
        print(string.format("%d,$%04X,$%04X,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.0f,%d,%d",
          n, blk, owner, var, 1, w(owner + 0x18), w(owner + 0x1C), ohead, w(owner + 0xC0) >> 8, w(owner + 0xE6) // 2,
          px, py, w(0x102A), w(0x10C0) >> 8, w(0x10E6) // 2, dist, rel, s(w(0x10EA)), s(w(owner + 0xEA))))
      end
    end
    last[blk] = { key = key, moved = moved }
  end
end)
