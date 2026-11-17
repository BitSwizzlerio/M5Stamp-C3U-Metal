; motor.il: the board's button (G9) starts a motor (G4), which keeps itself
; running through a seal-in contact until Stop (G10). A fan (G5) follows 2 s later.
; G9 reads 0 while the button is held, so "held" is LDN G9: a normally
; closed contact (XIO, "examine if open", on Allen-Bradley PLCs).
; Wiring: none to start it. For Stop, a switch or wire from G10 to 3V3;
; without one, Stop ladder in the viewer (or plc.stop()) ends it.
; Optional LEDs on G4 and G5.

LDN  G9      ; start: the button
OR   G4      ; seal-in
ANDN G10     ; stop
OUT  G4      ; motor

LD   G4      ; motor
TON  T0 2000 ; wait 2 s
OUT  G5      ; fan
