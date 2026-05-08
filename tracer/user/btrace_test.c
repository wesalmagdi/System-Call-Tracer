// btrace_test.c — Comprehensive test suite for Feature B: -e trace= filtering
//
// Confirmed behavior from actual runs:
//   - strace echo hi              → exec + write appear
//   - strace -e trace=write echo  → only write appears
//   - strace -e trace=read echo   → nothing (echo doesn't call read)
//   - strace -e trace=            → nothing (empty = trace nothing)
//   - strace -e trace=blah echo   → error + exit, no trace
//   - no -e flag                  → trace everything
//
// How to run each test:
//   Run the shell command shown above each group from the xv6 shell.
//   MANUAL lines tell you what to look for in the trace output.
//   PASS/FAIL lines are automated checks on program behavior itself.
//
// Add to Makefile: $U/_btrace_test
// Run as:         strace btrace_test
//                 strace -e trace=read btrace_test
//                 etc.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int passed = 0;
static int failed = 0;

static void
print_section(const char *name)
{
  printf("\n=== %s ===\n", name);
}

static void
expect(const char *label, int condition)
{
  if(condition){
    printf("  PASS: %s\n", label);
    passed++;
  } else {
    printf("  FAIL: %s\n", label);
    failed++;
  }
}

// ---------------------------------------------------------------------------
// GROUP 1: No -e flag — trace everything
// Run: strace btrace_test
// ---------------------------------------------------------------------------

static void
test_no_flag_traces_all(void)
{
  print_section("GROUP 1: No -e flag traces everything");

  int fd = open("README", O_RDONLY);
  expect("open README succeeds", fd >= 0);

  char buf[16];
  int n = read(fd, buf, 8);
  expect("read returns bytes", n > 0);

  write(1, buf, n);
  close(fd);
  getpid();

  printf("  MANUAL: open, read, write, close, getpid all appear in trace\n");
  printf("  MANUAL: no syscalls should be missing\n");
}

// ---------------------------------------------------------------------------
// GROUP 2: Single syscall filters
// ---------------------------------------------------------------------------

// Run: strace -e trace=read btrace_test
static void
test_filter_read_only(void)
{
  print_section("GROUP 2a: -e trace=read");

  char buf[16];
  int fd = open("README", O_RDONLY);  // should NOT appear
  expect("open succeeds", fd >= 0);
  int n = read(fd, buf, 8);           // SHOULD appear
  expect("read returns bytes", n > 0);
  write(1, buf, n);                   // should NOT appear
  close(fd);                          // should NOT appear

  printf("  MANUAL: ONLY read lines appear\n");
  printf("  MANUAL: open, write, close must NOT appear\n");
}

// Run: strace -e trace=write btrace_test
static void
test_filter_write_only(void)
{
  print_section("GROUP 2b: -e trace=write");

  char buf[16];
  int fd = open("README", O_RDONLY);  // should NOT appear
  int n = read(fd, buf, 8);           // should NOT appear
  expect("read ok", n > 0);
  write(1, buf, n);                   // SHOULD appear
  close(fd);                          // should NOT appear

  printf("  MANUAL: ONLY write lines appear\n");
  printf("  MANUAL: open, read, close must NOT appear\n");
}

// Run: strace -e trace=open btrace_test
static void
test_filter_open_only(void)
{
  print_section("GROUP 2c: -e trace=open");

  int fd = open("README", O_RDONLY);  // SHOULD appear
  expect("open succeeds", fd >= 0);
  char buf[8];
  read(fd, buf, 4);                   // should NOT appear
  close(fd);                          // should NOT appear

  printf("  MANUAL: ONLY open lines appear\n");
}

// Run: strace -e trace=close btrace_test
static void
test_filter_close_only(void)
{
  print_section("GROUP 2d: -e trace=close");

  int fd = open("README", O_RDONLY);  // should NOT appear
  char buf[8];
  read(fd, buf, 4);                   // should NOT appear
  close(fd);                          // SHOULD appear

  printf("  MANUAL: ONLY close lines appear\n");
}

// Run: strace -e trace=getpid btrace_test
static void
test_filter_getpid_only(void)
{
  print_section("GROUP 2e: -e trace=getpid");

  int fd = open("README", O_RDONLY);  // should NOT appear
  close(fd);                          // should NOT appear
  int pid = getpid();                 // SHOULD appear
  expect("getpid positive", pid > 0);

  printf("  MANUAL: ONLY getpid lines appear\n");
}

// Run: strace -e trace=fork btrace_test
static void
test_filter_fork_only(void)
{
  print_section("GROUP 2f: -e trace=fork");

  int pid = fork();                   // SHOULD appear
  if(pid == 0){
    exit(0);
  } else {
    expect("fork returns child pid", pid > 0);
    wait(0);                          // should NOT appear
  }

  printf("  MANUAL: ONLY fork line appears\n");
  printf("  MANUAL: wait and exit must NOT appear\n");
}

// Run: strace -e trace=exec btrace_test
static void
test_filter_exec_only(void)
{
  print_section("GROUP 2g: -e trace=exec");

  int pid = fork();
  if(pid == 0){
    char *argv[] = { "echo", "exectest", 0 };
    exec("echo", argv);               // SHOULD appear
    exit(1);
  } else {
    wait(0);
  }

  printf("  MANUAL: exec line appears for child\n");
  printf("  MANUAL: fork and wait must NOT appear\n");
}

// Run: strace -e trace=fstat btrace_test
static void
test_filter_fstat_only(void)
{
  print_section("GROUP 2h: -e trace=fstat");

  struct stat st;
  int fd = open("README", O_RDONLY);  // should NOT appear
  expect("open ok", fd >= 0);
  int r = fstat(fd, &st);             // SHOULD appear
  expect("fstat ok", r == 0);
  close(fd);                          // should NOT appear

  printf("  MANUAL: ONLY fstat lines appear\n");
}

// ---------------------------------------------------------------------------
// GROUP 3: Multi-syscall filters
// ---------------------------------------------------------------------------

// Run: strace -e trace=read,write btrace_test
static void
test_filter_read_write(void)
{
  print_section("GROUP 3a: -e trace=read,write");

  char buf[16];
  int fd = open("README", O_RDONLY);  // should NOT appear
  int n = read(fd, buf, 8);           // SHOULD appear
  expect("read ok", n > 0);
  write(1, buf, n);                   // SHOULD appear
  close(fd);                          // should NOT appear

  printf("  MANUAL: read and write appear\n");
  printf("  MANUAL: open and close must NOT appear\n");
}

// Run: strace -e trace=open,close btrace_test
static void
test_filter_open_close(void)
{
  print_section("GROUP 3b: -e trace=open,close");

  char buf[16];
  int fd = open("README", O_RDONLY);  // SHOULD appear
  read(fd, buf, 8);                   // should NOT appear
  write(1, buf, 4);                   // should NOT appear
  close(fd);                          // SHOULD appear

  printf("  MANUAL: open and close appear\n");
  printf("  MANUAL: read and write must NOT appear\n");
}

// Run: strace -e trace=read,write,open,close btrace_test
static void
test_filter_four_syscalls(void)
{
  print_section("GROUP 3c: -e trace=read,write,open,close");

  char buf[16];
  int fd = open("README", O_RDONLY);
  int n = read(fd, buf, 8);
  expect("read ok", n > 0);
  write(1, buf, n);
  close(fd);

  printf("  MANUAL: open, read, write, close all appear\n");
  printf("  MANUAL: getpid or other syscalls must NOT appear\n");
}

// Run: strace -e trace=fork,getpid btrace_test
static void
test_filter_fork_getpid(void)
{
  print_section("GROUP 3d: -e trace=fork,getpid");

  getpid();                           // SHOULD appear
  int pid = fork();
  if(pid == 0){
    exit(0);
  } else {
    expect("fork ok", pid > 0);
    wait(0);
  }

  printf("  MANUAL: fork and getpid appear\n");
  printf("  MANUAL: wait and exit must NOT appear\n");
}

// Run: strace -e trace=write,exec btrace_test
// Confirmed working in actual test run
static void
test_filter_write_exec(void)
{
  print_section("GROUP 3e: -e trace=write,exec (confirmed working)");

  int pid = fork();
  if(pid == 0){
    char *argv[] = { "echo", "hi", 0 };
    exec("echo", argv);               // SHOULD appear
    exit(1);
  } else {
    wait(0);
  }
  write(1, "done\n", 5);             // SHOULD appear

  printf("  MANUAL: exec and write appear\n");
  printf("  MANUAL: fork and wait must NOT appear\n");
}

// ---------------------------------------------------------------------------
// GROUP 4: Empty filter — trace nothing
// Run: strace -e trace= btrace_test
// ---------------------------------------------------------------------------

static void
test_empty_filter(void)
{
  print_section("GROUP 4: -e trace= (empty — trace nothing)");

  int fd = open("README", O_RDONLY);
  expect("open ok", fd >= 0);
  char buf[8];
  read(fd, buf, 4);
  write(1, buf, 4);
  close(fd);
  getpid();

  printf("  MANUAL: ZERO trace lines should appear\n");
  printf("  MANUAL: program still runs correctly (output appears)\n");
}

// ---------------------------------------------------------------------------
// GROUP 5: Edge cases
// ---------------------------------------------------------------------------

// Run: strace -e trace=read,read btrace_test
static void
test_duplicate_name_in_filter(void)
{
  print_section("GROUP 5a: -e trace=read,read (duplicate name)");

  char buf[8];
  int fd = open("README", O_RDONLY);
  read(fd, buf, 4);
  close(fd);

  printf("  MANUAL: read appears once per actual call (no crash, no double print)\n");
  printf("  MANUAL: open and close must NOT appear\n");
}

// Run: strace -e trace=read, btrace_test  (trailing comma)
static void
test_trailing_comma(void)
{
  print_section("GROUP 5b: -e trace=read, (trailing comma)");

  char buf[8];
  int fd = open("README", O_RDONLY);
  read(fd, buf, 4);
  close(fd);

  printf("  MANUAL: no crash — empty token ignored, read still traced\n");
  printf("  MANUAL: open and close must NOT appear\n");
}

// Run: strace -e trace=,read btrace_test  (leading comma)
static void
test_leading_comma(void)
{
  print_section("GROUP 5c: -e trace=,read (leading comma)");

  char buf[8];
  int fd = open("README", O_RDONLY);
  read(fd, buf, 4);
  close(fd);

  printf("  MANUAL: no crash — leading comma ignored, read still traced\n");
}

// Run: strace -e trace=read,,write btrace_test  (double comma)
static void
test_double_comma(void)
{
  print_section("GROUP 5d: -e trace=read,,write (double comma)");

  char buf[8];
  int fd = open("README", O_RDONLY);
  int n = read(fd, buf, 4);
  write(1, buf, n);
  close(fd);

  printf("  MANUAL: no crash — empty token ignored, read and write traced\n");
}

// ---------------------------------------------------------------------------
// GROUP 6: Unknown syscall name — error and exit
// Run manually from xv6 shell (process exits, can't test from inside binary)
// ---------------------------------------------------------------------------

static void
test_unknown_name_instructions(void)
{
  print_section("GROUP 6: Unknown name — manual shell tests");

  printf("  Run these from the xv6 shell:\n\n");

  printf("  $ strace -e trace=blah echo hi\n");
  printf("  Expected: strace: unknown syscall name 'blah'\n");
  printf("  Expected: process exits, 'hi' never prints, no trace output\n\n");

  printf("  $ strace -e trace=read,blah echo hi\n");
  printf("  Expected: error on 'blah', exits before tracing anything\n\n");

  printf("  $ strace -e trace=blah,read echo hi\n");
  printf("  Expected: error on 'blah' (first unknown name found)\n\n");

  printf("  $ strace -e trace=123 echo hi\n");
  printf("  Expected: error (numeric ids not accepted as names)\n\n");

  printf("  CONFIRMED working from actual run:\n");
  printf("  strace -e trace=blah echo hi → strace: unknown syscall name 'blah'\n");
}

// ---------------------------------------------------------------------------
// GROUP 7: Regression — smoke tests still pass
// ---------------------------------------------------------------------------

static void
test_regression_instructions(void)
{
  print_section("GROUP 7: Regression smoke tests");

  printf("  Run from xv6 shell after every change:\n\n");

  printf("  $ strace stracetest1\n");
  printf("  Expected: exec, getpid, fork, wait all appear\n\n");

  printf("  $ strace stracetest2\n");
  printf("  Expected: open(\"README\", 0) path prints correctly\n\n");

  printf("  $ strace stracetest3\n");
  printf("  Expected: sbrk(4096) — ONE argument, positive return value\n\n");

  printf("  $ strace echo hi\n");
  printf("  Expected: exec and write appear (hi may mix on same line — normal)\n");
}

// ---------------------------------------------------------------------------
// GROUP 8: Stress tests
// ---------------------------------------------------------------------------

// Run: strace -e trace=read btrace_test
static void
test_stress_many_reads(void)
{
  print_section("GROUP 8a: Stress — many reads under filter");

  char buf[4];
  int fd = open("README", O_RDONLY);
  expect("open ok", fd >= 0);

  int total = 0;
  int n;
  while((n = read(fd, buf, 1)) > 0)
    total += n;

  close(fd);
  expect("read some bytes", total > 0);

  printf("  MANUAL: many read lines appear, all complete\n");
  printf("  MANUAL: no kernel panic or truncated lines\n");
  printf("  MANUAL: close must NOT appear\n");
}

// Run: strace -e trace=fork btrace_test
static void
test_stress_many_forks(void)
{
  print_section("GROUP 8b: Stress — 5 forks under fork filter");

  for(int i = 0; i < 5; i++){
    int pid = fork();
    if(pid == 0)
      exit(0);
    expect("fork ok", pid > 0);
    wait(0);
  }

  printf("  MANUAL: 5 fork lines appear\n");
  printf("  MANUAL: wait and exit must NOT appear\n");
  printf("  MANUAL: no kernel panic\n");
}

// ---------------------------------------------------------------------------
// GROUP 9: Forward-looking (after -o and Feature C are merged)
// ---------------------------------------------------------------------------

static void
test_forward_looking_instructions(void)
{
  print_section("GROUP 9: Forward-looking (after -o and Feature C merged)");

  printf("  Once -o is implemented:\n");
  printf("  $ strace -e trace=read -o out.log btrace_test\n");
  printf("  Expected: only read lines in out.log, terminal clean\n\n");

  printf("  Once child tracing (Feature C) is implemented:\n");
  printf("  $ strace -e trace=fork btrace_test\n");
  printf("  Expected: fork from parent appears\n");
  printf("  Expected: child exit does NOT appear (not in filter)\n\n");

  printf("  $ strace -e trace=write btrace_test\n");
  printf("  Expected: write from both parent and child appear\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int
main(void)
{
  printf("btrace_test: Feature B -e filtering test suite\n");
  printf("===============================================\n");
  printf("Confirmed behavior:\n");
  printf("  no -e flag    -> trace everything\n");
  printf("  -e trace=x,y  -> trace only x and y\n");
  printf("  -e trace=     -> trace nothing\n");
  printf("  unknown name  -> error message + exit(1)\n\n");

  test_no_flag_traces_all();

  test_filter_read_only();
  test_filter_write_only();
  test_filter_open_only();
  test_filter_close_only();
  test_filter_getpid_only();
  test_filter_fork_only();
  test_filter_exec_only();
  test_filter_fstat_only();

  test_filter_read_write();
  test_filter_open_close();
  test_filter_four_syscalls();
  test_filter_fork_getpid();
  test_filter_write_exec();

  test_empty_filter();

  test_duplicate_name_in_filter();
  test_trailing_comma();
  test_leading_comma();
  test_double_comma();

  test_unknown_name_instructions();
  test_regression_instructions();

  test_stress_many_reads();
  test_stress_many_forks();

  test_forward_looking_instructions();

  printf("\n===============================================\n");
  printf("Automated checks: %d passed, %d failed\n", passed, failed);
  printf("See MANUAL lines above for trace output verification.\n");
  printf("Add $U/_btrace_test to UPROGS in Makefile to build.\n");

  exit(0);
}
