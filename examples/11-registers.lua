-- 11-registers.lua: blink GPIO4 by writing the chip's GPIO registers yourself.
--
-- gpio.write() is a small C function that writes to two hardware registers.
-- poke() can write to them directly, and peek() reads one back. A register is
-- just an address; each bit of these is one pin, so GPIO4 is bit 4: 1 << 4.
--
--   GPIO_OUT_W1TS  0x60004008  write 1s to set those pins' outputs to 1
--   GPIO_OUT_W1TC  0x6000400C  write 1s to clear them to 0
--   GPIO_IN        0x6000403C  the level of every pin, one bit each
--
-- "W1TS" means "write 1 to set": only the pins whose bits are 1 change, so
-- nothing else is disturbed.
--
-- In the viewer: G4 blinks exactly as in 02-blink.lua. Click it: the card says
-- it is driven by the GPIO_OUT register, just as when gpio.write() drives it,
-- because that is the register gpio.write() changes too.
-- The console shows GPIO_IN in hexadecimal; bit 9, the button, is set (0x200)
-- until you press it.
--
-- Press Stop to end it.
--
-- Try this: chapter 8 of the manual explains these registers. Make GPIO4 an
-- output with poke() too, using GPIO_ENABLE_W1TS at 0x60004024, instead of
-- gpio.output(4). (The pin also has to be connected to GPIO_OUT in the GPIO
-- matrix, which gpio.output() has already done here.)

GPIO_OUT_W1TS = 0x60004008
GPIO_OUT_W1TC = 0x6000400C
GPIO_IN       = 0x6000403C
PIN = 4
BIT = 1 << PIN

gpio.output(PIN)
while true do
  poke(GPIO_OUT_W1TS, BIT)
  print("set:   GPIO_IN = " .. hex(peek(GPIO_IN)) .. ", so GPIO4 is " .. ((peek(GPIO_IN) >> PIN) & 1))
  delay(500)
  poke(GPIO_OUT_W1TC, BIT)
  print("clear: GPIO_IN = " .. hex(peek(GPIO_IN)) .. ", so GPIO4 is " .. ((peek(GPIO_IN) >> PIN) & 1))
  delay(500)
end
