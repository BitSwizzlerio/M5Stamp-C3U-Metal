-- 02-blink.lua: blink GPIO4 and the RGB LED together, twice a second.
--
-- In the viewer: G4 turns into an output, and its circle fills in orange while
-- it is 1 and empties while it is 0. G4 joins the History by itself, where the
-- blinking draws a square wave. Click G4 to see that it is "driven by the
-- GPIO_OUT register".
--
-- Nothing needs to be wired. To see it for real, connect an LED and a 330 ohm
-- resistor in a line from GPIO4 to GND, with the LED's longer leg towards GPIO4.
--
-- Press Stop to end it.
--
-- Try this: change the two delays. What do 100 and 900 look like in the History?

gpio.output(4)
while true do
  gpio.write(4, 1)
  led(0, 20, 0)
  delay(250)
  gpio.write(4, 0)
  led(0, 0, 0)
  delay(250)
end
