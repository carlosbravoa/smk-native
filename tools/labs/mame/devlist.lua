for tag, dev in pairs(manager.machine.devices) do
  if tag:find("smp") or tag:find("sound") or tag:find("spc") or tag:find("dsp") or tag:find("apu") then
    local sp = ""
    if dev.spaces then for n, _ in pairs(dev.spaces) do sp = sp .. n .. " " end end
    print("DEV " .. tag .. " (" .. dev.shortname .. ") spaces: " .. sp)
  end
end
for tag, _ in pairs(manager.machine.memory.regions) do if tag:find("smp") or tag:find("sound") or tag:find("spc") then print("REGION " .. tag) end end
for tag, _ in pairs(manager.machine.memory.shares) do if tag:find("smp") or tag:find("sound") or tag:find("spc") or tag:find("apu") then print("SHARE " .. tag) end end
manager.machine:exit()
