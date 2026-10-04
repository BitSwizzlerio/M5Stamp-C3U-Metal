-- 06-traffic-light.lua: a traffic light on GPIO4 (red), GPIO5 (amber) and
-- GPIO6 (green), shown on the RGB LED as well.
--
-- This is the UK sequence: red, red and amber together, green, amber, red.
--
-- In the viewer: the three pins join the History, where you can see red and
-- amber overlap, and green never touch red.
--
-- To build one for real: three LEDs, each with a 330 ohm resistor, from GPIO4,
-- GPIO5 and GPIO6 to GND.
--
-- Press Stop to end it.
--
-- Try this: change it to the US sequence, which goes straight from red to green,
-- with no red-and-amber step.

RED, AMBER, GREEN = 4, 5, 6
gpio.output(RED)
gpio.output(AMBER)
gpio.output(GREEN)

function lights(r, a, g, ms)
  gpio.write(RED, r)
  gpio.write(AMBER, a)
  gpio.write(GREEN, g)
  led(r * 30 + a * 30, a * 15 + g * 30, 0)
  delay(ms)
end

while true do
  lights(1, 0, 0, 3000)     -- stop
  lights(1, 1, 0, 1000)     -- get ready
  lights(0, 0, 1, 3000)     -- go
  lights(0, 1, 0, 1000)     -- stop, if it is safe to
end
