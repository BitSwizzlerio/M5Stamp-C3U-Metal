# The ladder engine, without a board

The ladder engine ([app/plc.c](../app/plc.c), [plc_asm.c](../app/plc_asm.c) and
[plc_show.c](../app/plc_show.c)) is plain C. This folder runs that same C, unchanged,
in two places other than the board:

- **on a PC**, as tests that need no board and run in GitHub Actions;
- **in a browser**, compiled to WebAssembly: the **C3U Simulator**, which is the
  C3U Viewer's own page with a simulated board in place of a real one.

The engine reaches the pins through `REG()`, the register macro from
[chip/esp32c3-regs.h](../chip/esp32c3-regs.h). Here, [host/esp32c3-regs.h](host/esp32c3-regs.h)
points `REG()` at [hw.c](hw.c), a pretend ESP32-C3 with just enough of the chip for
the engine: the GPIO registers, the IO_MUX pull resistors, and a clock that moves only
when the test or the browser moves it.

| File | What it holds |
|---|---|
| [hw.c](hw.c), [sim.h](sim.h) | The simulated chip: pins, pulls, the button, the clock. |
| [host/esp32c3-regs.h](host/esp32c3-regs.h) | The real register header, with `REG()` sent to `hw.c`. |
| [test_ladder.c](test_ladder.c) | The PC tests: every example program drawn, every error message, the examples' behaviour, and 3,000 mangled programs. |
| [expected.txt](expected.txt) | What the tests print. A change to it shows up as a difference. |
| [fuzz_asm.c](fuzz_asm.c), [fuzz.dict](fuzz.dict) | A libFuzzer target: the assembler fed whatever the fuzzer can think of. |
| [wasm_api.c](wasm_api.c) | The few functions the simulator's page calls in the WebAssembly build. |
| [test_wasm.js](test_wasm.js) | A check of the WebAssembly build in Node. |
| [build.py](build.py) | Builds and runs all of the above. |

## The tests

```
python sim/build.py test               # gcc, or $CC
python sim/build.py test --sanitize    # with AddressSanitizer and UBSan (Linux)
python sim/build.py fuzz 60            # a minute of fuzzing (clang on Linux)
```

On Windows, gcc from MSYS2 works. [ladder-tests.yml](../.github/workflows/ladder-tests.yml)
runs them all on every change to the engine.

## The simulator

```
python sim/build.py wasm               # the engine, as viewer/c3uview/web/plc.wasm
python sim/build.py site               # the page, into sim/out/site
python -m http.server -d sim/out/site  # then open http://localhost:8000
```

`wasm` needs the [WASI SDK](https://github.com/WebAssembly/wasi-sdk/releases): set
`WASI_SDK_PATH` to where it is unpacked. The built `plc.wasm` is committed, so the
viewer and the page can be built without it; `ladder-tests.yml` rebuilds it and checks
that it hasn't fallen behind the C.

The page is the viewer's `index.html`, changed a little by
[viewer/c3uview/simpage.py](../viewer/c3uview/simpage.py), and run by
[viewer/c3uview/web/sim.js](../viewer/c3uview/web/sim.js) instead of the viewer program.
`sim.js` sends the page the same events the viewer program sends, and reads and writes
`plc_image` in the WebAssembly memory with the same offsets the viewer uses to reach it
over JTAG. The Ladder Logic card, the operator panel and the ladder challenges all work
unchanged. Lua doesn't: that needs a real board.

The viewer serves the same page at `/sim` (its **Simulate** button), and
[pages.yml](../.github/workflows/pages.yml) publishes it on GitHub Pages.
