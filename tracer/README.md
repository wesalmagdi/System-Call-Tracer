# System Call Tracer (strace for xv6)

A strace-like tool built inside the xv6 operating system kernel that 
intercepts and logs all system calls made by a process, outputting 
structured JSON for analysis.

## Output Format

One JSON object per line, newline-delimited:

```json
{"timestamp":144,"pid":3,"process":"cat","syscall":"open","args":["README",0,8242],"return":3}
{"timestamp":144,"pid":3,"process":"cat","syscall":"read","args":[3,4112,512],"return":512}
{"timestamp":144,"pid":3,"process":"cat","syscall":"write","args":[1,4112,512],"return":512}
{"timestamp":144,"pid":3,"process":"cat","syscall":"close","args":[3,4112,512],"return":0}
```

### Field descriptions
| Field | Type | Description |
|-------|------|-------------|
| timestamp | integer | xv6 ticks since boot |
| pid | integer | process ID |
| process | string | program name |
| syscall | string | syscall name |
| args | array | arguments (strings for paths, integers for others) |
| return | integer | return value (negative = error) |

---

## Usage

```bash
# Trace any command
strace cat README
strace echo hello
strace grep hello README

# Normal commands are NOT traced
cat README   # no JSON output
```

---

## How to apply to your xv6

Copy the files maintaining folder structure into your `xv6-labs-2025`:

```bash
cp kernel/* ~/xv6-labs-2025/kernel/
cp user/strace.c ~/xv6-labs-2025/user/
# merge user/user.h and user/usys.pl manually
# merge Makefile UPROGS manually
make qemu
strace cat README
```

---

## Files Changed

| File | What was added |
|------|----------------|
| `kernel/syscall.c` | trace_syscall() + hook in dispatcher + syscall_names[] |
| `kernel/proc.h` | `trace_enabled` flag in struct proc |
| `kernel/proc.c` | initialize flag + fork() inheritance |
| `kernel/sysproc.c` | sys_trace() implementation |
| `kernel/syscall.h` | `#define SYS_trace 22` |
| `user/strace.c` | user-space strace command |
| `user/user.h` | `int trace(int)` declaration |
| `user/usys.pl` | trace entry |
| `Makefile` | added `$U/_strace` to UPROGS |

---

## Tested

| Test | Command | Result |
|------|---------|--------|
| Basic trace | `strace cat README` | ✅ |
| Echo trace | `strace echo hello` | ✅ |
| No trace without strace | `cat README` | ✅ |
| Multi-arg trace | `strace grep hello README` | ✅ |
| Fork inheritance | `strace echo hello` (child traced) | ✅ |
| String args | open shows filename, exec shows program | ✅ |

---

## Known Issues / TODO

- [ ] `exit` syscall not captured (process dies before hook fires)
- [ ] `fork` args should be `[]` not raw registers
- [ ] Code cleanup pass needed

---

## Architecture
