; numbers.il: count presses of the button in D0, and light G4 from 5.
; At 8 the count starts again from 0. Lua can read and change it:
; plc.get("D0"), plc.set("D0", 0). Wiring: none (optional LED on G4).

LDF  G9          ; each press of the button ...
ADD  D0 1 D0     ; ... adds 1 to D0

LD>= D0 5        ; five presses or more ...
OUT  G4          ; ... light G4

LD>= D0 8        ; at eight ...
MOV  0 D0        ; ... start again from 0
