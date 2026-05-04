# System Call Tracer — Core Tracing Module

## What this does
Intercepts all syscalls made by a process and outputs
structured JSON — one line per syscall.

## How to apply to xv6
Copy kernel/ and user/ files into your xv6-labs-2025
maintaining folder structure, then:
make qemu
strace cat README

## Usage
strace <command>
strace cat README
strace echo hello
strace grep hello README

## Output format
One JSON object per line:
{"timestamp":144,"pid":3,"process":"cat","syscall":"open","args":["README",0,8242],"return":3}

## Fields
- timestamp: xv6 ticks since boot
- pid: process ID
- process: program name
- syscall: syscall name
- args: arguments (strings for path args, integers for others)
- return: return value (negative = error)

## Files changed
| File | Change |
|------|--------|
| kernel/syscall.c | main hook + trace_syscall() + syscall_names[] |
| kernel/proc.h | added trace_enabled flag to struct proc |
| kernel/proc.c | initialize flag + fork() inheritance |
| kernel/sysproc.c | sys_trace() implementation |
| kernel/syscall.h | SYS_trace = 22 |
| user/strace.c | user-space strace command |
| user/user.h | trace() declaration |
| user/usys.pl | trace entry |
| Makefile | added strace to UPROGS |

## Tested
- strace cat README
- strace echo hello 
- cat README (no output) 
- strace grep hello README 
- fork inheritance 

## TODO
- Fix exit syscall capture
- Clean fork args to []