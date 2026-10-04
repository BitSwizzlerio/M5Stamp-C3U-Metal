-- 04-button.lua: GPIO4 and the LED follow the board's button.
--
-- The button is on GPIO9. It connects the pin to ground, so GPIO9 reads 0
-- while it is pressed; button() turns that round, and is true while pressed.
--
-- In the viewer: hold the button and watch G9 go from 1 to 0, the drawn button
-- sink, and G4 go to 1 at the same moment. Both are in the History.
--
-- Press Stop to end it.
--
-- Try this: make GPIO4 do the opposite, on while the button is NOT pressed.

gpio.output(4)
was = nil
while true do
  now = button()
  if now ~= was then
    gpio.write(4, now)
    if now then led(0, 0, 30) else led(0, 0, 0) end
    print(now and "pressed" or "released")
    was = now
  end
  delay(10)
end
