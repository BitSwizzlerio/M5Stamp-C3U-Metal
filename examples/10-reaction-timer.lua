-- 10-reaction-timer.lua: how fast can you press the button?
--
-- The LED turns red: get ready. After a random wait it turns green: press the
-- button as fast as you can. The console shows your time in milliseconds, and
-- your best. Press too early and it tells you.
--
-- millis() counts milliseconds since the board started, so the difference
-- between two readings is the time in between.
--
-- In the viewer: G9, the button, drops to 0 the moment you press. Most people
-- take 200 to 300 milliseconds.
--
-- Press Stop to end it.
--
-- Try this: use micros() instead of millis() for a time in microseconds.

-- Wait 'ms' milliseconds, but give up early if the button is pressed: true if it was.
function wait_unless_pressed(ms)
  local start = millis()
  while millis() - start < ms do
    if button() then return true end
    delay(5)
  end
  return false
end

function wait_for_release()
  while button() do delay(10) end
end

best = nil
print("When the LED turns green, press the button as fast as you can.")
while true do
  led(30, 0, 0)
  if wait_unless_pressed(math.random(1500, 4000)) then
    led(30, 15, 0)
    print("Too early! Wait for green.")
  else
    led(0, 30, 0)
    start = millis()
    while not button() do end
    time = millis() - start
    led(0, 0, 0)
    if best == nil or time < best then
      best = time
      print("Your time: " .. time .. " ms. That's your best yet!")
    else
      print("Your time: " .. time .. " ms. Your best is " .. best .. " ms.")
    end
  end
  wait_for_release()
  delay(1500)
end
