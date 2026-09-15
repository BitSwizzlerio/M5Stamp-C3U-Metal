# Solution to exercise 6: Ctrl-C while typing

Changed file: `app/repl.c`.

## What changed

- `read_line()` now returns `bool`, and gives the line's length through a pointer. It returns `false` when Ctrl-C is pressed. That way "cancelled" can't be confused with an empty line.
- `push_line()` passes that on. When the line was cancelled it pushes nothing and returns `false`.
- A new status, `STATUS_CANCELLED` (-1), means "Ctrl-C, nothing to run". Lua's own status codes are never negative, so it can't clash with them.
  - `load_line()` returns it when Ctrl-C is pressed at the `>` prompt.
  - `multiline()` returns it at the `>>` prompt, abandoning the statement.
- `repl_run()` checks for `STATUS_CANCELLED` first. It clears the Lua stack and shows a new prompt, without running or reporting anything.

## The Lua stack

When `multiline()` gives up, the text of the statement so far is still at index 1 of the stack. `load_line()` removes it with `lua_remove(L, 1)`, as it always does, before returning the status. `repl_run()` then calls `lua_settop(L, 0)` anyway. The stack is empty again whichever way the statement ended.

## Tested on the board

```
> for i = 1, 3 do
>> ^C
> print(1)
1
> ^C
> for i = 1, 2 do
>>
>> print(i)
>> end
1
2
> while true do end
interrupted!
> print("ok")
ok
```

`python tools/selftest.py`: 15 of 15.
