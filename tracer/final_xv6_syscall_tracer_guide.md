# xv6 strace — Team Guide (Plain Language)

This document explains what's broken in our strace implementation, what tests to build.

---

## What does strace do?

`strace` is a tool that watches a running program and prints every **system call** it makes — meaning every time the program asks the kernel to do something (open a file, fork a child process, write to the screen, etc.).

Our version works like this:

1. You run `strace myprogram` from the xv6 shell.
2. `strace` turns on a "tracing" flag for itself, then runs `myprogram` in its place.
3. For every syscall `myprogram` makes, the kernel prints a line like:

```
<pid>: syscall <name>(<args>) -> <return value>
```

---

## What the smoke tests showed (confirmed by running them)

We ran `stracetest1`, `stracetest2`, and `stracetest3`. Here is exactly what came out:

```
$ strace echo zakaria > file1.txt
3: syscall exec("echo", 16312) -> 2
3: syscall write(1, 16352, 7) -> 7
```

`echo` should produce at least 3 trace lines (`exec`, `write`, `exit`) but only 2 appear. `exit` is missing entirely, and `exec` returns `2` which is wrong.

```
$ strace stracetest1
4: syscall exec("stracetest1", 16328) -> 1
4: syscall getpid() -> 4
4: syscall fork() -> 5
4: syscall wait(16316) -> 5

$ strace stracetest2
6: syscall exec("stracetest2", 16328) -> 1
6: syscall open("README", 0) -> 3
6: syscall fstat(3, 16280) -> 0
6: syscall dup(3) -> 4
6: syscall close(4) -> 0
6: syscall close(3) -> 0

$ strace stracetest3
7: syscall exec("stracetest3", 16328) -> 1
7: syscall uptime() -> 2378
7: syscall sbrk(4096, 1) -> 16384
7: syscall pause(2) -> 0
7: syscall uptime() -> 2380
```

**What is working:**
- All expected syscalls appear for each test: `getpid`, `fork`, `wait`, `open`, `fstat`, `dup`, `close`, `uptime`, `sbrk`, `pause`.
- File path arguments are decoded correctly: `open("README", 0)`, `exec("stracetest1", ...)`.
- The three smoke test programs exist and run.

**What is visibly wrong in the output above:**
- `exec` returns `1` in every test — a successful exec should never return at all (Bug 9).
- `exit` never appears at the end of any test — the program ends silently (Bug 9).
- `sbrk` shows two arguments: `sbrk(4096, 1)` — `sbrk` only takes one argument (Bug 11).
- After `fork() -> 5`, nothing from PID 5 ever appears — the child is completely invisible (Bug 1).

---

## Part 1 — Bugs to Fix

**P1 = real correctness bug** — something is wrong or silently broken. Fix these first.

---

### Bug 1 — Child processes are invisible to the tracer (P1)
**Owner: Member A**
**Confirmed by:** `stracetest1` — `fork() -> 5` appears but zero lines from PID 5 follow.

**What you see:** The parent calls `fork()` and gets back the child's PID (5). But every syscall the child makes — including its own `exit` — is completely invisible. It is as if the child never ran.

**Why it happens:** Every new process starts with `trace_enabled = 0`. When the parent forks a child, the child gets a fresh process slot but nobody copies the parent's tracing flag into it.

**The fix:** In the `kfork` function, after the child process is created, copy the flag:

```c
// In kfork(), after allocproc() succeeds:
np->trace_enabled = p->trace_enabled;
```

**Side effect to expect:** After this fix, child and parent lines will mix on the screen. That is normal — they both print to the same console.

---

### Bug 2 — A variable is set but never actually used (P1)
**Owner: Member A**

**What you see:** Nothing visibly breaks — but there is dead code in the kernel that could mislead future readers.

**Why it happens:** There is a global variable called `trace_target_pid` set to `-1`. The kernel checks it, but `sys_trace` never writes to it — so the check never passes. It is code that can never run.

**The fix (choose one):**
- **Safest:** Delete the variable and the check entirely.
- **More work:** Wire it up properly with `sys_trace` writing to it, and protect it with a spinlock since multiple processes could access it at the same time.

---

### Bug 3 — Trace output and program output mix on the screen (P1)
**Owner: Member C**

**What you see:** The program's own printed text and the strace output appear tangled on the same line. Example:

```
k3: syscall write(1, ...)
```

Here the `k` is from the program's output and `3: syscall...` is the trace line — they collided.

**What to investigate:** Is this just ugly, or can it cause a deadlock? If the trace `printf` triggers another syscall while a kernel lock is already held, the kernel could freeze. Read `kernel/printf.c` and confirm that `trace_syscall` is never called while any kernel lock is held.

---

### Bug 4 — File paths longer than 63 characters get cut off in the trace (P1)
**Owner: Member B**

**What you see:** If a syscall uses a path longer than 63 characters, the trace line shows a shortened version. The actual syscall still works — only the printed output is wrong.

**Why it happens:** The tracer copies the path into a 64-byte buffer: 63 characters plus 1 null terminator. Anything longer gets silently cut.

**What to test:** Call `open()` with paths of length 30, 60, 63, 64, 65, and 100 characters. Check:
- Does the trace end cleanly or does it print garbage after the cutoff?
- Does `printf` stop at the right place?

**The fix:** Raise the buffer size to 128 bytes (the kernel's `MAXPATH` constant), or at minimum document exactly where truncation starts.

---

### Bug 5 — Return values are printed as 32-bit numbers (P1)
**Owner: Member B**

**What you see:** Some syscalls return 64-bit values (like memory addresses from `sbrk`). The tracer casts the return value to `int` before printing, which cuts off the top half. The result can look negative or just wrong.

**The fix:**

```c
// Current (wrong):
printf("-> %d", (int)ret);

// Fixed:
printf("-> %ld", (long)ret);
```

**How to verify:** Call `sbrk(0)` several times and watch the printed addresses. They should grow steadily. If any look negative or jump backward, the bug is still active.

---

### Bug 6 — The 1-byte-write filter hides real writes (P1)
**Owner: Member B**

**What you see:** If a program deliberately calls `write(1, &c, 1)` to write one character to the screen, the trace never shows it. The filter cannot tell the difference between a `printf`-driven 1-byte write and an intentional one — both look identical.

**What to investigate:** Is there a smarter way to filter — for example by checking the caller's program counter, or adding a per-process "raw mode" flag? Or should we just document the limitation clearly so no one wastes time on it later?

---

### Bug 7 — The `trace()` call itself never appears in the trace (informational)
**Owner: Member A**

**What you see:** When you run `strace`, the `trace()` syscall that enables tracing is not printed. This is probably expected: the kernel snapshots the "should I trace?" flag before `sys_trace` flips it, so the flag is still `0` when the snapshot is taken.

**What to do:** Confirm this is intentional and write a 2-sentence note. The goal is just to stop the next person from re-investigating it.

---

### Bug 8 — exec's argument list shows as a raw memory address (informational)
**Owner: Member C**
**Confirmed by:** every smoke test — e.g. `exec("stracetest1", 16328)`.

**What you see:** The path is decoded correctly (`"stracetest1"`), but the second argument — the list of arguments passed to the program — shows up as a raw number (`16328`) instead of the actual arguments.

**What to do:** Do not decode the argument list (that would be a new feature, which is out of scope). Just confirm that the number printed is actually a valid memory address and not garbage. This tells us the tracer is reading the right CPU register.

---

### Bug 9 — `exec` shows the wrong return value and `exit` never appears (P1)
**Owner: Member C**
**Confirmed by:** all three smoke tests and `strace echo zakaria`.

**What you see — example 1:** Running `strace echo zakaria > file1.txt` only prints 2 lines total:

```
3: syscall exec("echo", 16312) -> 2
3: syscall write(1, 16352, 7) -> 7
```

`echo` should also call `exit()` when it finishes — that line never appears. Only 2 syscalls are shown when there should be at least 3.

**What you see — example 2:** The smoke tests have the same problem:

```
4: syscall exec("stracetest1", 16328) -> 1   ← wrong, should not return on success
4: syscall getpid() -> 4
4: syscall fork() -> 5
4: syscall wait(16316) -> 5
                                              ← exit(0) is completely missing
```

**Two separate problems here:**

**Problem A — exec returns 1:** A successful `exec` replaces the current program entirely and never returns to the caller. A return value of `1` means the tracer is printing the return value at the wrong moment — either before exec runs, or by reading a stale register left over from before the exec happened.

**Problem B — exit never appears:** Every program calls `exit` when it finishes. The fact that it never appears in any trace means the tracer completely misses it. This is because `exit` in xv6 calls an internal kernel function that terminates the process directly — it never comes back through the normal syscall dispatcher where the tracer runs, so the tracer never gets a chance to print it.

**What to investigate:**
- In `kernel/syscall.c`, find where the tracer prints the return value. Is it running before or after the syscall executes?
- In `kernel/proc.c`, find the `exit` function. Does it go through the syscall dispatcher? If not, add a trace print directly inside the exit handling code before the process is terminated.
- Check every syscall in the table: does each one pass through the tracer both before and after execution?

**How to confirm it's fixed:** Run `strace stracetest1`. The output must end with:
```
4: syscall exit(0)
```
And `exec` should show `-> 0` or not appear at all (since a successful exec never returns).

---

### Bug 10 — `strace ls` prints too many unnecessary lines (P1)
**Owner: Member C**

**What you see:** Running `strace ls` floods the screen with so many lines that the output is unreadable. Far more lines appear than `ls` should need.

**What the correct output should look like:** `ls` reads one directory and prints file names. The trace should be short and follow this pattern:

```
open(".", 0) -> 3
fstat(3, ...) -> 0
read(3, addr, 16) -> 16     ← repeated once per file in the directory
open("./filename", 0) -> 4
fstat(4, ...) -> 0
close(4) -> 0
...
read(3, addr, 16) -> 0      ← signals end of directory
close(3) -> 0
exit(0)
```

**What to investigate:**
- Is the 1-byte write filter (Bug 6) broken for `ls`? `ls` uses `printf` internally — if the filter is not working, each printed character becomes a separate `write(1, addr, 1)` trace line, flooding the output.
- Are there extra syscalls from xv6's startup code being traced before `ls` even starts?
- Is any syscall being printed twice? The dispatcher might be calling the trace function more than once per syscall.

**How to confirm it's fixed:** Run `strace ls`. For a directory with around 20 files, the total number of trace lines should be under 100.

---

### Bug 11 — `sbrk` shows two arguments when it only takes one (P1)
**Owner: Member B**
**Confirmed by:** `stracetest3` output — `sbrk(4096, 1)` instead of `sbrk(4096)`.

**What you see:**

```
7: syscall sbrk(4096, 1) -> 16384
```

`sbrk` takes exactly one argument: how many bytes to grow the heap. The `1` being printed is garbage — it comes from a CPU register that happens to hold leftover data from something else. The tracer reads it as a second argument because its argument-count table has the wrong number for `sbrk`.

**The fix:** Find `sbrk` in the syscall argument-count table and change its count from `2` to `1`.

**How to verify:** After the fix, `sbrk(4096)` should appear with exactly one argument and no trailing value.

---

## Part 2 — Tests to Build

Each test is a small program you add to `user/` and register in the `Makefile`. Run each test as `strace <testname>` from the xv6 shell.

---

### T1 — Existing smoke tests (already in the repo)
Re-run these after every single change to confirm nothing broke.

| Test | Syscalls it exercises | What to check |
|---|---|---|
| `stracetest1` | `exec`, `getpid`, `fork`, `wait`, `exit` | After fixes: all five appear; child PID visible; `exec` return is correct; `exit` appears |
| `stracetest2` | `exec`, `open`, `fstat`, `dup`, `close` | File descriptors are 3 then 4; `exec` return is correct after Bug 9 fix |
| `stracetest3` | `exec`, `uptime`, `sbrk`, `pause` | `sbrk` shows exactly one argument after Bug 11 fix; second `uptime` ≥ first + 2 |

---

### T2 — Error returns trace correctly
**Tests:** Bug 9 / Member A

Write a program that intentionally fails three syscalls, then makes one successful call at the end.

```
open("nonexistent_file", 0)   → should show -> -1
kill(99999)                   → should show -> -1
unlink("nonexistent_file")    → should show -> -1
getpid()                      → should show the correct PID
```

**Pass if:** All three failures show `-> -1`. The final `getpid` returns the right PID and traces normally.

---

### T3 — File descriptors stay in the valid range
**Tests:** Bug 5 / Member B

Open the same file 5 times, saving each file descriptor. Close them in reverse order. Open the file one more time.

**Pass if:** Every file descriptor in the trace is between 3 and 15 (xv6's max is 15). After closing and reopening, the new descriptor should be 3 again (reused). If you see 16 or higher, the tracer is reading the wrong register.

---

### T4 — Long paths get truncated safely, not corrupted
**Tests:** Bug 4 / Member B

Call `open()` on paths of length 30, 60, 63, 64, 65, and 100 characters. Use `mkdir` to build long paths if needed.

**Pass if:**
- You can identify the exact length where the trace starts truncating.
- Truncated paths end cleanly — no garbage characters after the cutoff.
- The actual syscall still succeeds or fails based on whether the file exists, not based on the truncated copy in the trace.

Document your findings in a comment in the code.

---

### T5 — Child processes appear in the trace after Bug 1 is fixed
**Depends on:** Bug 1 fix / Member A

Write a program where the parent forks 3 children. Each child calls `getpid()`, then `exit()`. The parent waits for all three.

**Pass if:** The trace shows `getpid` and `exit` lines for each of the 3 child PIDs, plus the parent's `fork` and `wait`. Lines from different PIDs will interleave — that is fine and expected.

---

### T6 — Tracing continues across exec
**Tests:** integration / Member C

Write program A that execs program B. B does a few syscalls, then execs program C. C does a few syscalls and exits.

**Pass if:** All three programs' syscalls appear under the **same PID** (exec replaces the program but keeps the process). Both `exec` lines show the correct path string. This is a regression check for the exec pre-fetch fix.

---

### T7 — A failed exec traces the path correctly
**Tests:** Bug 9 / Member B

Write a program that calls `exec("/no/such/binary", argv)` — this should fail with `-1`. Then call `getpid()`, then exit.

**Pass if:** The trace shows `exec("/no/such/binary", ...) -> -1`. Since exec failed, the program's memory was not replaced, so `getpid` and `exit` trace normally after.

---

### T8 — Tracer survives heavy load
**Tests:** stability / Member C

Write a program that loops 1000 times calling `getpid()`, then calls `sbrk(0)` 100 times, then exits.

**Pass if:** All 1100 trace lines appear. The program completes without a kernel panic or missing lines.

---

### T9 — An untraced program does not appear in another process's trace
**Tests:** isolation / Member C

Start a background program (no strace) that loops calling `getpid()`. Then run `strace stracetest1` in the foreground.

**Pass if:** The trace contains only the foreground program's PID. Nothing from the background program appears.

---

### T10 — `cat` still works correctly after the 1-byte filter
**Tests:** regression / Member C

Run `strace cat README`.

**Pass if:** The trace shows multi-byte `read` and `write` pairs like `read(3, addr, 512)` and `write(1, addr, N)`. No flood of 1-byte write lines. The file content displays correctly between trace lines.

---

### T11 — `ls` directory walk traces correctly (currently broken — see Bug 10)
**Tests:** Bug 10 / Member C

Run `strace ls`.

**Current behavior (wrong):** Too many lines — the screen floods and is unreadable.

**Pass if:** The trace shows this clean pattern:
1. `open(".", 0) -> 3`
2. `fstat(3, ...)`
3. Repeated `read(3, addr, 16) -> 16` (one per file)
4. For each file: `open("./filename", 0) -> 4`, `fstat(4, ...)`, `close(4)`
5. Final `read(3, addr, 16) -> 0` (signals end of directory)
6. `close(3)`
7. `exit(0)`

File descriptors never go above 4. For a directory with ~20 files, total trace lines should be under 100.

---

### T12 — `sbrk` shows one argument and correct return values
**Tests:** Bug 11 + Bug 5 / Member B

Write a program that calls `sbrk` four times:

```
sbrk(0)     → baseline heap address
sbrk(4096)  → returns old address (heap grows by 4096)
sbrk(4096)  → returns old address again (heap grows another 4096)
sbrk(0)     → returns current top of heap
```

**Pass if:**
- Each `sbrk` line shows exactly one argument — no trailing garbage like `sbrk(4096, 1)`.
- All four return values are positive integers.
- The second and third values differ by exactly 4096.
- The fourth equals the third plus 4096.

---

### T13 — Confirm what happens when `trace()` traces itself
**Tests:** Bug 7 / Member A

Write a program that calls `trace()` and then exits.

**Pass if:** You determine whether `trace()` itself appears as a trace line or not. Either answer is fine — just write it down so the next person does not re-investigate it.

---

## Part 3 — Who Owns What

| Member | Bugs to fix | Tests to build |
|---|---|---|
| **Member A** — kernel correctness | Bug 1 (child invisible), Bug 2 (dead variable), Bug 7 (self-trace) | T2, T5, T13 |
| **Member B** — boundary cases | Bug 4 (path truncation), Bug 5 (32-bit return), Bug 6 (1-byte filter), Bug 11 (sbrk args) | T3, T4, T7, T12 |
| **Member C** — integration & regression | Bug 3 (console mixing), Bug 8 (exec argv address), Bug 9 (missing exit + exec return), Bug 10 (ls flood) | T1, T6, T8, T9, T10, T11 |

---

## Part 4 — Rules Every Fix Must Follow

Every change submitted must satisfy all five of these:

1. **Don't break the smoke tests.** Run `stracetest1`, `stracetest2`, and `stracetest3` before and after your change. Both runs must pass.

2. **Include a matching test.** Every changed code path must be covered by at least one test from Part 2 (or a new test if none fits).

3. **Write a short note.** Add 2–3 sentences to this document explaining what changed and any new edge cases you discovered.

4. **Survive `usertests`.** Run xv6's built-in test suite. No new failures allowed.

5. **Never crash on bad input.** Any place you call `copyin` or `fetchstr` (functions that read user memory), check the return value. If it fails, print the raw address instead of the string. Never let the kernel panic because of a bad user pointer.

---

## Part 5 — What We Are NOT Doing

To keep the project focused, the following are off-limits:

- Timestamps on trace lines
- Filtering trace output by syscall name
- Decoding the `argv` array in `exec`
- Decoding `read`/`write` buffer contents
- Decoding `fstat` structs
- A switch to turn tracing off mid-run
- Logging trace output to a file
- Tracing kernel-internal function calls (only user-issued syscalls are in scope)
