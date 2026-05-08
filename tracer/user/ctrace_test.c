#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static int passed = 0;
static int failed = 0;

static void
section(const char *n)
{
    printf("\n=== %s ===\n", n);
}

static void
expect(const char *l, int c)
{
    if (c) {
        printf("  PASS: %s\n", l);
        passed++;
    } else {
        printf("  FAIL: %s\n", l);
        failed++;
    }
}

static void
test_single_child(void)
{
    section("GROUP 1: single child inherits trace");

    int pid = fork();

    if (pid == 0) {
        getpid();
        exit(42);
    }

    expect("fork ok", pid > 0);

    int st;
    wait(&st);

    expect("child exited 42", st == 42);
    printf("  MANUAL: child's getpid line appears with CHILD's pid\n");
}

static void
test_multiple(void)
{
    section("GROUP 2: 3 children all visible");

    for (int i = 0; i < 3; i++) {
        int pid = fork();

        if (pid == 0) {
            getpid();
            exit(i + 100);
        }

        expect("fork ok", pid > 0);
    }

    for (int i = 0; i < 3; i++) {
        int st;
        wait(&st);
    }

    printf("  MANUAL: 3 distinct child pids show getpid lines\n");
}

static void
test_grandchild(void)
{
    section("GROUP 3: grandchild via 2 forks");

    int pid = fork();

    if (pid == 0) {
        int gpid = fork();

        if (gpid == 0) {
            getpid();
            exit(7);
        }

        int st;
        wait(&st);
        exit(st);
    }

    int st;
    wait(&st);

    expect("grandchild exited 7", st == 7);
    printf("  MANUAL: 3 levels of pids visible\n");
}

static void
test_storm(void)
{
    section("GROUP 4: 5 sequential forks");

    for (int i = 0; i < 5; i++) {
        int pid = fork();

        if (pid == 0) {
            getpid();
            exit(0);
        }

        expect("fork ok", pid > 0);
        wait(0);
    }

    printf("  MANUAL: no panic, 5 children visible\n");
}

int
main(void)
{
    passed = 0; failed = 0; printf("ctrace_test: Feature C suite\n");
    printf("============================\n");

    test_single_child();
    test_multiple();
    test_grandchild();
    test_storm();

    printf("\n============================\n");
    printf("Automated checks: %d passed, %d failed\n", passed, failed);

    exit(0);
}
