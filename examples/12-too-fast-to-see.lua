-- 12-too-fast-to-see.lua: what the viewer can't show, and why.
--
-- The viewer looks at the pins about 20 times a second. This blinks GPIO4
-- faster and faster, 4 seconds at each speed. While the blinking is slow, the
-- History draws it faithfully. Once a whole on-and-off takes less than two of
-- the viewer's looks (about 100 ms), it can't keep up: the wave turns ragged,
-- seems to slow down, or freezes. Sampling too slowly to catch a signal is
-- called aliasing; it's why wagon wheels in films can seem to turn backwards.
--
-- Nothing is wrong with the board. An LED on GPIO4 (330 ohm resistor to GND)
-- shows the real blinking, until it is too fast for your eyes as well.
--
-- It stops by itself after about 30 seconds.
--
-- Try this: the console prints each speed. At which one did the History
-- stop looking right? And at which one did an LED seem to stay lit?

gpio.output(4)
for _, period in ipairs({1000, 400, 200, 100, 60, 40, 20, 10}) do
  print(string.format("one blink every %4d ms: %5.1f blinks a second", period, 1000 / period))
  start = millis()
  while millis() - start < 4000 do
    gpio.write(4, 1)
    delay(period // 2)
    gpio.write(4, 0)
    delay(period // 2)
  end
end
print("Done. Scroll back through the console and compare with what the History showed.")
