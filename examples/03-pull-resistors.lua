-- 03-pull-resistors.lua: what a pull resistor does, with nothing wired at all.
--
-- An input pin with nothing connected isn't 0 or 1: it floats, and reads
-- whatever charge happens to sit on it. A pull-up resistor inside the chip
-- gently holds it at 1; a pull-down holds it at 0. A button then only has to
-- connect the pin to the other level.
--
-- In the viewer: G5 is an input (green). Its circle fills while it reads 1.
-- Click it, and the details show which pull resistor is on. Every two seconds
-- this switches between pull-up, pull-down and no pull at all.
--
-- Press Stop to end it.
--
-- Try this: watch "no pull" closely. It usually keeps whatever level it had
-- before, because nothing is moving the charge. Touch the GPIO5 pin with a
-- finger while it has no pull, and see what happens.

gpio.input(5)
while true do
  gpio.input(5, "up")
  delay(50)
  print("pull-up:   GPIO5 reads", gpio.read(5))
  delay(2000)
  gpio.input(5, "down")
  delay(50)
  print("pull-down: GPIO5 reads", gpio.read(5))
  delay(2000)
  gpio.input(5, "none")
  delay(50)
  print("no pull:   GPIO5 reads", gpio.read(5))
  delay(2000)
end
