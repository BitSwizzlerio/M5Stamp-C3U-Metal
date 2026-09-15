# Exercise 6: Ctrl-C while typing

Type the first line of a loop and then press Ctrl-C:

```
> for i = 1, 3 do
>> ^C
>>
```

The console threw away the line you were typing, but it is still waiting for the rest of the `for` loop. The only way out is to finish the statement or type something that is a syntax error. Ctrl-C should abandon the whole statement and go back to the `>` prompt.

## How the console reads a statement

In `app/repl.c`:

- `read_line()` reads keys until Enter. On Ctrl-C it prints `^C` and returns an empty line.
- `push_line()` shows a prompt, calls `read_line()`, and pushes the text onto the Lua stack.
- `load_line()` tries the line as an expression (`add_return()`), then as a statement (`multiline()`).
- `multiline()` compiles what it has. If Lua says the statement isn't finished (the error message ends in `<eof>`), it calls `push_line()` with the `>>` prompt, joins the new line on, and tries again.
- `repl_run()` runs whatever `load_line()` compiled, or prints the error.

The problem: an empty line from Ctrl-C looks exactly like pressing Enter on an empty line, which is a normal thing to do in the middle of a statement.

## What to change

1. Give `read_line()` a way to say "cancelled" that is different from an empty line, and pass that up through `push_line()`.
2. In `multiline()`, stop when the line was cancelled. Keep track of what is on the Lua stack: `repl_run()` calls `lua_settop(L, 0)` after each statement, which clears it.
3. `load_line()` returns a status. Pick one that `repl_run()` can recognise as "nothing to run, nothing to report", or add a new one of your own.

## Check

- `for i = 1, 3 do`, then Ctrl-C: back to `>`. Then `print(1)` prints 1.
- Ctrl-C at the `>` prompt still just gives a new prompt.
- `while true do end` followed by Ctrl-C still says `interrupted!`.
- An empty line in the middle of a statement still works: `for i = 1, 2 do`, Enter, `print(i)`, `end`.
- `python tools/selftest.py` passes.
