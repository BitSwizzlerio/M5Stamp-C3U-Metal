; flasher.il: two timers flash G4 and G5 in turn, like a level crossing.
; Holding the button (G9 reads 0 while held) stops the flashing.
; Wiring: optional LEDs on G4 and G5.
; @lamp   G4 Left red
; @lamp   G5 Right red
; @show   T0 Flash time

LDN  T1      ; T1 not done ...
AND  G9      ; ... and the button not held
TON  T0 500  ; T0: 500 ms

LD   T0      ; T0 done
TON  T1 500  ; T1: 500 ms more

LD   T0
OUT  G4      ; first light

LDN  T0
AND  G9      ; button not held
OUT  G5      ; second light
