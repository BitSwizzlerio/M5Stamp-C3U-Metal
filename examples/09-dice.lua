-- 09-dice.lua: press the button to roll a dice.
--
-- While the button is held the dice tumbles; let go and it lands. The number
-- shows in binary on GPIO4 (1s), GPIO5 (2s) and GPIO6 (4s), as a colour on the
-- RGB LED, and in the console.
--
-- math.random gives different numbers every time the board starts, because
-- C3U-Metal seeds it from the chip's own random number generator.
--
-- In the viewer: watch G4, G5 and G6 flicker while the dice tumbles, then
-- settle. Click them to read the number in binary.
--
-- Press Stop to end it.
--
-- Try this: roll 100 dice at once in the console and count how many sixes
-- come up:  n = 0 for i = 1, 100 do if math.random(1, 6) == 6 then n = n + 1 end end print(n)

PINS = {4, 5, 6}
COLOURS = { {40, 0, 0}, {40, 20, 0}, {30, 30, 0}, {0, 40, 0}, {0, 0, 40}, {30, 0, 30} }
for _, p in ipairs(PINS) do gpio.output(p) end

function show(n)
  for b = 0, #PINS - 1 do
    gpio.write(PINS[b + 1], (n >> b) & 1)
  end
  led(table.unpack(COLOURS[n]))
end

print("Press the button to roll the dice.")
while true do
  if button() then
    while button() do
      show(math.random(1, 6))
      delay(60)
    end
    roll = math.random(1, 6)
    show(roll)
    print("You rolled a " .. roll)
  end
  delay(10)
end
