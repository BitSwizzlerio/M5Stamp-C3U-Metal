; motor.il: the board's button (G9) or Start on the operator panel starts a
; motor (G4), which keeps itself running through a seal-in contact until Stop.
; A fan (G5) follows 2 s later.
; G9 reads 0 while the button is held, so "held" is LDN G9: a normally
; closed contact (XIO, "examine if open", on Allen-Bradley PLCs).
; Wiring: none. Stop is on the operator panel; a switch or wire from G10 to
; 3V3 stops it too. Optional LEDs on G4 and G5.
; @button M0 Start
; @button M1 Stop
; @lamp   G4 Motor
; @lamp   G5 Fan green
; @show   T0 Fan delay

LDN  G9      ; start: the button
OR   M0      ; or Start on the panel
OR   G4      ; seal-in
ANDN G10     ; stop
ANDN M1      ; or Stop on the panel
OUT  G4      ; motor

LD   G4      ; motor
TON  T0 2000 ; wait 2 s
OUT  G5      ; fan
