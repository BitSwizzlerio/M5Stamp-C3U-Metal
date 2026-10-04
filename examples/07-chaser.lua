-- 07-chaser.lua: a light that runs up and down the board's left-hand pins.
--
-- The pins are listed in the order they sit along the left edge of the board,
-- from G3 at the top to G0 at the bottom, so in the viewer the light really
-- does run down the edge and back.
--
-- show(lit) writes true to the pin whose place in the list is 'lit', and false
-- to all the others: i == lit is true for exactly one of them.
--
-- Press Stop to end it.
--
-- Try this: light two pins at once, side by side. Or make the light speed up as
-- it goes.

pins = {3, 4, 5, 6, 7, 8, 10, 1, 0}
for _, p in ipairs(pins) do gpio.output(p) end

function show(lit)
  for i, p in ipairs(pins) do
    gpio.write(p, i == lit)
  end
end

while true do
  for i = 1, #pins do
    show(i)
    delay(90)
  end
  for i = #pins - 1, 2, -1 do
    show(i)
    delay(90)
  end
end
