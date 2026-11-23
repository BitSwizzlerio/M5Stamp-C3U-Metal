; toggle.il: each press of the button (G9) turns G4 on or off,
; and C0 counts the presses. Read them from Lua with plc.get("M0")
; and plc.get("C0"). Wiring: none (optional LED on G4).
; @lamp   G4 Light amber
; @show   C0 Presses

LDF  G9      ; just pressed ...
ANDN M0      ; ... while off
OUT  M1      ; turn on

LDF  G9      ; just pressed ...
AND  M0      ; ... while on
OUT  M2      ; turn off

LD   M1
SET  M0      ; on
LD   M2
RST  M0      ; off

LD   M0
OUT  G4      ; light

LDF  G9      ; press
CTU  C0 1000 ; count
