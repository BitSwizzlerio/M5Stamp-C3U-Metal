; motor.il: Start (G8) runs a motor (G4), which keeps itself running
; through a seal-in contact until Stop (G10). A fan (G5) follows 2 s later.
; Wiring: switches from G8 and G10 to 3V3; optional LEDs on G4 and G5.

LD   G8      ; start
OR   G4      ; seal-in
ANDN G10     ; stop
OUT  G4      ; motor

LD   G4      ; motor
TON  T0 2000 ; wait 2 s
OUT  G5      ; fan
