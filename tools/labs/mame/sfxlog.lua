-- Every sound the game ASKS FOR, per frame, with the player's state.
--
-- $81:F57A is the play-sound call (A = id); $81:F5E2 queues it at
-- $0E6C,x with the index in $0E6A (max 6 = three a frame) and a second
-- word at $0E74,x.  Polling the queue each frame catches every request
-- without a breakpoint (MAME's bpset is dead in this rig).
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local function w(a) return mem:read_u16(0x7E0000 + a) end
local n = 0
print("frame,id,arg,speed,drive,state,x,y,mode")
emu.register_frame_done(function()
  n = n + 1
  local q = w(0x0E6A)
  if q == 0 or q > 6 then return end
  local m = mem:read_u8(0x7E0036) // 2
  for i = 0, q - 2, 2 do
    print(string.format("%d,%04X,%04X,%d,%02X,%02X,%d,%d,%d",
      n, w(0x0E6C + i), w(0x0E74 + i), w(0x10EA),
      mem:read_u8(0x7E10AC), mem:read_u8(0x7E10A6),
      w(0x1018), w(0x101C), m))
  end
end)
