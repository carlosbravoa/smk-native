for tag, dev in pairs(manager.machine.devices) do
  if tag == ":soundcpu" then
    for name, item in pairs(dev.items or {}) do print("ITEM " .. name) end
  end
end
local ok, err = pcall(function()
  for name, _ in pairs(manager.machine.save.items or {}) do
    if name:find("smp") or name:find("timer") or name:find("sound") then print("SAVE " .. name) end
  end
end)
manager.machine:exit()
