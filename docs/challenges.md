# Challenges

The C3U Viewer's **Challenges** button opens a card of tasks. The viewer watches the board's pins, about 20 times a second, and ticks each task off once it has seen it done. There is nothing to submit and nothing for a teacher to mark. Progress is remembered in the browser.

Ladder challenges only appear while the **Ladder Logic** card is open, so a student who hasn't got to ladder logic sees only the Lua ones. A ladder challenge only counts when the ladder drives the pins: done in Lua, it says so instead of ticking.

## The challenges

| Challenge | Done when |
|---|---|
| **Lua:** Light G4 | G4 is an output and reads 1 for 2 seconds |
| G4 follows the button | for 3 presses, G4 lights while the button is held and is off soon after it's let go |
| Blink G4 at 2 Hz | G4 blinks at 2 Hz, within 15%, for 5 seconds in a row |
| A 3-second timer | after a press, G4 comes on within half a second and stays on for 3 seconds, within 0.35 s |
| SOS in Morse on G4 | the last nine flashes are three short, three long, three short; long ones are at least twice as long |
| **Ladder:** A seal-in | after a press, G4 stays on for 2 seconds after the button is let go |
| Count to five | G4 stays off for four presses and comes on at the fifth |
| A level-crossing flasher | G4 and G5 take turns at about 1 Hz, never on together, for 5 seconds |
| Light G4 from 10 | G4 follows "D0 is 10 or more", seen both ways |
| A fan that runs on | G5 stays on for 2 seconds, within 0.35 s, after G4 goes off |

The viewer only sees a pin about every 50 ms, so the timings allow for that, and a task can't ask for anything faster than a few changes a second.

## Adding a challenge

The challenges are a list at the top of [viewer/c3uview/web/challenges.js](../viewer/c3uview/web/challenges.js). Each one is a few lines:

```js
{ id: "blink", group: "Lua", title: "Blink G4 at 2 Hz",
  task: "Make G4 turn on and off twice a second, and keep it going for 5 seconds.",
  hint: "A loop: write 1, <code>delay(250)</code>, write 0, <code>delay(250)</code>.",
  check: { type: "blink", pin: 4, hz: 2, seconds: 5 } },
```

- `id` is a short name, different for each challenge. It is what the browser remembers.
- `group` is `"Lua"` or `"Ladder"`.
- `task` and `hint` are HTML, so `<code>` works.
- `check` picks one of the checks below and fills in its numbers. Add `ladder: true` for a challenge that the ladder, not Lua, must do.

| `type` | Settings | Passes when |
|---|---|---|
| `on` | `pin`, `seconds` | the pin is an output at 1 for that long |
| `follow` | `pin`, `input`, `presses` | the pin follows the button (`input`) for that many presses |
| `blink` | `pin`, `hz`, `seconds` | the pin blinks at `hz`, within 15%, for that long |
| `timer` | `pin`, `input`, `seconds` | a press turns the pin on for that long, within 0.35 s |
| `sos` | `pin` | the last nine flashes read S-O-S |
| `seal` | `pin`, `input`, `seconds` | the pin stays on that long after a press is let go |
| `count` | `pin`, `input`, `presses` | the pin stays off until that press, then comes on |
| `alternate` | `a`, `b`, `hz`, `seconds` | the two pins take turns at about `hz`, never on together |
| `compare` | `pin`, `reg`, `at` | the pin is on while D`reg` is `at` or more, seen both ways |
| `rundown` | `pin`, `follower`, `input`, `seconds` | the follower stays on that long after the pin goes off |

A new kind of check is a function in `CHECKS` in the same file. It gets the challenge's `check` settings, can look at the pins' recent history with `edges()`, `presses()`, `levelAt()` and `now()`, and returns `{ done, progress, text }`: whether it's done, how far along it is from 0 to 1, and a line telling the student what the viewer sees.

Changing the list means running the viewer from source ([viewer/README.md](../viewer/README.md)) or building it again. A viewer downloaded from a release has the list built in.
