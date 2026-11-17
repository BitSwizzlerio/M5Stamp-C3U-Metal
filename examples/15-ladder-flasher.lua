-- 15-ladder-flasher.lua: ladder logic. Two timers make G4 and G5 flash in turn,
-- like the lights at a level crossing.
--
-- Each timer starts the other one:
--   rung 0: T0 times while T1 is not done. After 500 ms, T0 is done ...
--   rung 1: ... which starts T1. After 500 ms, T1 is done, rung 0 breaks, T0 resets,
--           and so does T1: the cycle starts again.
--   rungs 2 and 3: G4 is on while T0 is done, G5 while it isn't.
-- Holding the button (G9) stops the flashing: G9 reads 0 while it's held.
--
-- Wiring: optional LEDs on G4 and G5.
-- Watch G4 and G5 take turns on the pin map. Press Stop to stop watching the rungs.

plc.load[[
LDN  T1
AND  G9       ; the button, not held
TON  T0 500
LD   T0
TON  T1 500
LD   T0
OUT  G4
LDN  T0
AND  G9
OUT  G5
]]
plc.run()
plc.watch()

-- Try: make G4's half 200 ms and G5's 800 ms. Which timer is which?
