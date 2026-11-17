-- 16-ladder-and-lua.lua: ladder logic and Lua working together.
--
-- The ladder turns the button into an on/off switch, the job ladder is good at:
--   rungs 0 and 1: a press (G9 falling to 0) turns M1 or M2 on for one scan,
--                  depending on whether the light is off or on;
--   rungs 2 and 3: M1 sets M0 and M2 resets it, so each press flips M0;
--   rung 4: G4 follows M0, and C0 counts the presses.
-- Lua does what ladder can't: it reads M0 and C0 with plc.get(), lights the RGB LED
-- and prints the count.
--
-- Wiring: none (an LED on G4 is optional). Press the button on the board.
-- Press Stop to end it; the ladder keeps running until plc.stop().

plc.load[[
LDF  G9       ; the button has just been pressed ...
ANDN M0       ; ... while the light is off
OUT  M1
LDF  G9       ; the button has just been pressed ...
AND  M0       ; ... while the light is on
OUT  M2
LD   M1
SET  M0
LD   M2
RST  M0
LD   M0
OUT  G4
LDF  G9
CTU  C0 1000  ; count the presses
]]
plc.run()

do
  local last = -1
  while true do
    if plc.get("M0") == 1 then led(0, 40, 0) else led(0, 0, 0) end
    local count = plc.get("C0")
    if count ~= last then
      print("presses: " .. count)
      last = count
    end
    delay(50)
  end
end

-- Try: hold the button down. Why does the light change only once?
