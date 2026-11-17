-- 14-ladder-motor.lua: ladder logic. A motor starts, keeps itself running, and stops.
--
-- The program between [[ and ]] is IL (Instruction List), a PLC's assembly language.
-- It runs on the board's ladder engine, not in Lua: Lua only loads it and starts it.
--
--   rung 0: the board's button (G9) starts the motor (G4). A seal-in contact, G4 itself,
--           keeps it running after the button is let go. Stop (G10) breaks the rung.
--   rung 1: the fan (G5) comes on once the motor has run for 2 seconds.
--
-- The button pulls G9 to 0 while it's held, so "held" is LDN G9: a normally closed
-- contact, [/G9 ] in the drawing (XIO, "examine if open", on Allen-Bradley PLCs).
--
-- Wiring: none to start it. Stop needs a switch or wire from G10 to 3V3; ladder inputs
-- have pull-down resistors, so with nothing connected G10 reads 0. No wire? plc.stop()
-- ends the ladder. Optional LEDs on G4 and G5.
-- The console redraws the rungs: "==" is a wire with power. Press Stop to stop watching;
-- the ladder keeps running until plc.stop().

plc.load[[
LDN  G9      ; start: the button
OR   G4      ; seal-in: the motor keeps itself on
ANDN G10     ; stop
OUT  G4      ; motor
LD   G4
TON  T0 2000 ; wait 2 s
OUT  G5      ; fan
]]
plc.run()
plc.watch()

-- Try: plc.list() shows each instruction and what it did in the last scan.
-- Then change 2000 to 5000 and run the file again.
