; tank.il: a water tank with an operator panel. The tank is simulated: D0 is
; its level, 0 to 100, which the ladder raises while the pump (G4) runs and
; lowers while the drain valve (G5) is open. In Auto, the pump keeps the level
; up to the setpoint in D1. Wiring: none (optional LEDs on G4 and G5).
; @group  Tank
; @bar    D0 Level 0 100
; @set    D1 Fill to 10 95
; @lamp   G4 Pump green
; @lamp   G5 Drain valve blue
; @group  Controls
; @switch M2 Auto
; @switch M1 Drain
; @button M0 Fill now
; @group  Alarms
; @lamp   M5 High level red

LD=  D1 0        ; no setpoint yet (D registers start at 0) ...
MOV  60 D1       ; ... so start at 60

LD   M2          ; Auto ...
AND< D0 D1       ; ... and below the setpoint
OR   M0          ; or Fill now held
ANDN M5          ; but never above the high level
OUT  G4          ; pump

LD   M1          ; Drain switched on
OUT  G5          ; drain valve

LD   G4          ; while the pump runs,
ANDN T0          ; every 200 ms ...
TON  T0 200
LD   T0
AND< D0 100
ADD  D0 1 D0     ; ... the level rises by 1

LD   G5          ; while the drain is open,
ANDN T1          ; every 300 ms ...
TON  T1 300
LD   T1
AND> D0 0
SUB  D0 1 D0     ; ... the level falls by 1

LD>= D0 95       ; nearly full
OUT  M5          ; high level alarm
