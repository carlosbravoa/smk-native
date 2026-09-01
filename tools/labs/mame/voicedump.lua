-- Every DSP voice, EVERY frame, around a poked sound: this is how an
-- effect is captured now (NOTES 213).  Reading the chip instead of the
-- speaker means the music cannot bleed in - the renderer keeps only the
-- voices that differ from a baseline run and rebuilds them from the
-- game's own BRR samples.
--
--   SFX_ID (hex, optional)  the sound to poke at SFX_START
--   SFX_START               the frame to poke on
local ram = manager.machine.devices[":soundcpu"].spaces["program"]
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function dsp(r)
  local s = ram:read_u8(0xF2); ram:write_u8(0xF2, r)
  local v = ram:read_u8(0xF3); ram:write_u8(0xF2, s); return v
end
local function w16(a, v)
  mem:write_u8(0x7E0000 + a, v & 0xFF); mem:write_u8(0x7E0001 + a, (v >> 8) & 0xFF)
end
-- SFX_ID takes a LIST ("3C,3F"): the game fires several ids for one
-- sound and the parts mean nothing alone (NOTES 215), so the queue -
-- which holds three - is poked with the whole group at once.
local ids = {}
for tok in string.gmatch(os.getenv("SFX_ID") or "", "[^,]+") do
  ids[#ids+1] = tonumber(tok, 16)
end
local start = tonumber(os.getenv("SFX_START") or "2200")
local after = tonumber(os.getenv("SFX_AFTER") or "150")
local n = 0
print("frame,voice,voll,volr,pitch,srcn,envx")
emu.register_frame_done(function()
  n = n + 1
  -- SFX_GAPF spaces the list out over frames instead of queueing it in
  -- one: two ids in the same frame land on the same voice and the
  -- second simply replaces the first (NOTES 215).
  -- SFX_ITEM=<hex>: hand the player that item READY and press A, so a
  -- state nobody recorded (a star, a lightning) can still be watched.
  local item = os.getenv("SFX_ITEM")
  if item and n == start then
    w16(0x0D70, 0xC000 | tonumber(item, 16))
    mem:write_u8(0x7E0EA0, 0x80)     -- the pad's A, for the frame
  end
  if #ids > 0 then
    local gap = tonumber(os.getenv("SFX_GAPF") or "0")
    if gap > 0 then
      for i, v in ipairs(ids) do
        if n == start + (i - 1) * gap then
          w16(0x0E6C, v); w16(0x0E74, 0); w16(0x0E6A, 2)
          print("FIRE " .. n .. " " .. string.format("%02X", v))
        end
      end
    elseif n == start then
      for i, v in ipairs(ids) do
        w16(0x0E6C + (i - 1) * 2, v)
        w16(0x0E74 + (i - 1) * 2, 0)
      end
      w16(0x0E6A, #ids * 2)
      print("FIRE " .. n .. " " .. (os.getenv("SFX_ID") or ""))
    end
  end
  if n < start - 4 then return end
  if n > start + after then manager.machine:exit() end
  for i = 0, 7 do
    local b = i * 16
    local vl, vr = dsp(b), dsp(b + 1)
    if vl > 127 then vl = vl - 256 end
    if vr > 127 then vr = vr - 256 end
    print(string.format("%d,%d,%d,%d,%04X,%02X,%d", n, i, vl, vr,
      dsp(b + 3) * 256 + dsp(b + 2), dsp(b + 4), dsp(b + 8)))
  end
end)
