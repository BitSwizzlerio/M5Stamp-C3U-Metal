-- 01-hello.lua: say hello, and light the RGB LED red, green, then blue.
--
-- In the viewer: the LED's data line is GPIO2, in the middle of the board
-- picture. led() sends each colour down it as a burst of pulses that lasts
-- about 30 microseconds, far too quick for the viewer to catch, so G2 stays
-- "out 0" even while the LED shines. The viewer shows what a pin is doing
-- about 20 times a second; anything faster slips between its looks.
--
-- Try this: change the colours. Each number is 0 (off) to 255 (as bright as
-- it goes), for red, green and blue. led(40, 40, 0) is yellow.

print("Hello from the M5Stamp C3U!")
led(40, 0, 0)
delay(600)
led(0, 40, 0)
delay(600)
led(0, 0, 40)
delay(600)
led(0, 0, 0)
print("Type help() to see everything the board adds to Lua.")
