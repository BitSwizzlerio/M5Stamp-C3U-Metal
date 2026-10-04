-- 13-demux.lua: a 1-of-4 demultiplexer. GPIO8 and GPIO10 choose which of GPIO4-7 is on.
--
--   GPIO8 GPIO10   output
--     0     0      GPIO4
--     0     1      GPIO5
--     1     0      GPIO6
--     1     1      GPIO7
--
-- Wiring: a switch from each input to GND. The pull-ups make an open switch read 1
-- and a pressed one 0, so with nothing pressed GPIO7 is on.
-- Press Stop to end it.

do
  local OUTPUTS = {4, 5, 6, 7}
  local INPUTS  = {8, 10}

  for _, p in ipairs(OUTPUTS) do gpio.output(p) end
  for _, p in ipairs(INPUTS) do gpio.input(p, "up") end

  local last = nil
  while true do
    local sel = gpio.read(8) * 2 + gpio.read(10)    -- 0, 1, 2 or 3
    if sel ~= last then
      if last then gpio.write(OUTPUTS[last + 1], 0) end
      gpio.write(OUTPUTS[sel + 1], 1)
      print("input " .. (sel >> 1) .. (sel & 1) .. " -> GPIO" .. OUTPUTS[sel + 1])
      last = sel
    end
    delay(10)
  end
end
