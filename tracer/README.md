# xv6 Syscall Tracer — Core Tracing Module

Intercepts every syscall made by a traced process and outputs one JSON line per syscall — to the console **and** to `/xv6_trace.log` inside xv6's filesystem.

---

## Output format

One JSON object per line (newline-delimited):

```json
{"timestamp":144,"pid":3,"process":"cat","syscall":"open","args":["README",0,8242],"return":3}
```

| Field | Meaning |
|-------|---------|
| `timestamp` | xv6 ticks since boot |
| `pid` | process ID |
| `process` | program name |
| `syscall` | syscall name (always a quoted string) |
| `args` | array — strings for path args, integers for others |
| `return` | return value (negative = error, e.g. -1) |

---

## How to apply to xv6-labs-2025

### Step 1 — Install dependencies (Ubuntu / WSL)

```bash
sudo apt update
sudo apt install -y git build-essential qemu-system-misc \
                    gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
```

### Step 2 — Clone the xv6-labs-2025 repo

```bash
git clone git://g.csail.mit.edu/xv6-labs-2025
cd xv6-labs-2025
```

### Step 3 — Copy the tracer files into the repo

Run these commands from **inside** the `xv6-labs-2025` directory.
Replace `<TRACER_DIR>` with the path to this tracer folder (e.g. `../tracer`).

```bash
# kernel files
cp <TRACER_DIR>/kernel/proc.h     kernel/proc.h
cp <TRACER_DIR>/kernel/proc.c     kernel/proc.c
cp <TRACER_DIR>/kernel/syscall.h  kernel/syscall.h
cp <TRACER_DIR>/kernel/syscall.c  kernel/syscall.c
cp <TRACER_DIR>/kernel/sysproc.c  kernel/sysproc.c

# user-space files
cp <TRACER_DIR>/user/strace.c  user/strace.c
cp <TRACER_DIR>/user/user.h    user/user.h
cp <TRACER_DIR>/user/usys.pl   user/usys.pl
```

> **Do NOT copy the Makefile.**
> The Makefile in xv6-labs-2025 already has `$U/_strace` added.
> Overwriting it would reset other lab configurations.

### Step 4 — Build

```bash
make clean
make qemu
```

`make clean` removes old compiled objects so all tracer files recompile from scratch.
QEMU starts and shows the xv6 shell prompt (`$`).

### Step 5 — Run strace inside the xv6 shell

```
$ strace cat README
```

JSON lines appear on the console in real time.
The same lines are also saved to `/xv6_trace.log` inside xv6's filesystem.

### Step 6 — Quit QEMU when done

```
Ctrl-A  then  X
```

### Step 7 — Get the log file to the backend automatically

Use `run_trace.sh` — it boots xv6, runs strace, saves the JSON, and quits, all without any manual steps.

**Install expect (one time only):**

```bash
# copy the script into the xv6 folder
cp <TRACER_DIR>/run_trace.sh ~/xv6-labs-2025/run_trace.sh
chmod +x ~/xv6-labs-2025/run_trace.sh

# run from inside the xv6 folder
cd ~/xv6-labs-2025
./run_trace.sh
```

xv6 boots and you have full keyboard control.
Run any strace commands you want, then quit with `Ctrl-A X`.
All JSON lines are saved automatically to `../backend/Data/xv6_trace.log`.

Open `run_trace.sh` and change the `OUTFILE` line at the top if your backend folder is in a different location.

---

## Files changed

> The Makefile already has `$U/_strace` — do not replace it.

| File | What was added / changed |
|------|--------------------------|
| `kernel/syscall.c` | `trace_syscall()` with kbuf JSON builder + `trace_write_log()` + hook in `syscall()` |
| `kernel/proc.h` | `trace_enabled` and `trace_logfd` fields added to `struct proc` |
| `kernel/proc.c` | initialize both fields in `allocproc()`; copy both in `kfork()` |
| `kernel/sysproc.c` | `sys_trace()` reads two args: pid and logfd |
| `kernel/syscall.h` | `#define SYS_trace 22` |
| `user/strace.c` | opens `/xv6_trace.log`, calls `trace(getpid(), logfd)`, then exec |
| `user/user.h` | `int trace(int, int)` declaration |
| `user/usys.pl` | `entry("trace")` stub |

---

## Compatibility note — xv6-labs-2025

This tracer was written and tested on **xv6-labs-2025**.
The labs version renames some internal kernel functions compared to the original xv6-riscv repo:

| Standard xv6-riscv | xv6-labs-2025 | Why |
|--------------------|---------------|-----|
| `fork()` in proc.c | `kfork()` | avoids name clash with user syscall wrapper |
| `exit()` in proc.c | `kexit()` | same reason |
| `wait()` in proc.c | `kwait()` | same reason |
| `kill()` in proc.c | `kkill()` | same reason |
| `sys_sleep()` | `sys_pause()` | renamed in labs version |

All tracer code uses the xv6-labs-2025 names, so there are **no conflicts**.

---

## How it works (step by step)

```
strace cat README
     │
     ├─ opens /xv6_trace.log  →  gets fd (e.g. 3)
     │
     ├─ calls trace(getpid(), 3)
     │       └─ kernel sys_trace():
     │               p->trace_enabled = 1
     │               p->trace_logfd   = 3
     │
     └─ exec("cat", ...)
             │
             │  [for every syscall cat makes]
             │
             └─ kernel syscall():
                     saves args (a0–a5) before execution
                     runs the syscall
                     if trace_enabled:
                         trace_syscall(p, num, saved_args)
                             │
                             ├─ builds JSON in kbuf (512-byte kernel buffer)
                             ├─ printf  →  console (always)
                             └─ trace_write_log  →  /xv6_trace.log
```

### Why the trapframe scratch trick?

`filewrite()` needs a **user virtual address**, but our JSON lives in a kernel buffer.
The trapframe page is 4096 bytes; `struct trapframe` uses only 288 bytes of it.
The leftover 3808 bytes are mapped in user space at `TRAPFRAME + 288`.
We copy our JSON there, then pass that user VA to `filewrite()`.

### Why save args before execution?

`a0` holds arg0 **before** the call, but becomes the **return value** after.
We snapshot all six argument registers before `syscalls[num]()` runs.

### Why trace `exit` before execution?

`exit()` never returns — the process is freed immediately.
Logging after would crash, so we log it before the call.

### Fork inheritance

`kfork()` copies `trace_enabled` and `trace_logfd` to the child.
This is why `strace sh` also traces every command the shell runs.

---

## Tested

| Command | What it verifies |
|---------|-----------------|
| `strace cat README` | open, read, write, close |
| `strace echo hello` | write, exit |
| `strace grep hello README` | open, read, write |
| `cat README` (no strace) | no output — tracing is off |
| `strace sh` | fork inheritance works |

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `strace: not found` | Confirm `$U/_strace` is in Makefile UPROGS; run `make clean && make qemu` |
| No JSON on console | Make sure you ran `strace <cmd>`, not just `<cmd>` |
| `/xv6_trace.log` empty | xv6 root filesystem must be writable; try `make clean && make qemu` |
| Kernel panic on strace | Null-check: add `if(p == 0) return;` at top of `trace_syscall` |
| Compile error about `sys_pause` | Your xv6 version may use `sys_sleep` — rename in `sysproc.c` and `syscall.c` |
| `debugfs` not found | Install with `sudo apt install e2tools` or use Option A instead |
