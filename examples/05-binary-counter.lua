-- 05-binary-counter.lua: count from 0 to 15 in binary on GPIO4 to GPIO7.
--
-- GPIO4 is the 1s bit, GPIO5 the 2s, GPIO6 the 4s and GPIO7 the 8s. The number
-- the four pins show goes up by one every 0.4 seconds.
--
-- In the viewer: the four pins join the History. Each one's wave is half as
-- fast as the one above it: that is what counting in binary looks like.
--
-- (n >> b) & 1 picks bit b out of n: >> shifts the bits right by b places, and
-- & 1 keeps only the lowest.
--
-- Press Stop to end it.
--
-- Try this: count backwards. Or add GPIO8 as a fifth bit and count to 31.

pins = {4, 5, 6, 7}
for _, p in ipairs(pins) do gpio.output(p) end
n = 0
while true do
  for b = 0, #pins - 1 do
    gpio.write(pins[b + 1], (n >> b) & 1)
  end
  print(n)
  n = (n + 1) % 16
  delay(400)
end
