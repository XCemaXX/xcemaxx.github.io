// waitpid(WNOHANG) that never returns -- minimal version.
//
// A thread of the dying child flushes its own /proc entries on the way out and
// gets preempted mid-kill inside __dentry_kill(). do_exit() has just expelled it
// to the root task group, where it is ineligible, and the autogroup's entity is
// kept runnable forever by the sched_yield spinners below -- so it is never
// picked again and the dentry stays half-killed. Meanwhile the parent reaps the
// zombie, walks the same /proc subtree, and spins forever in the unbounded retry
// loop of shrink_dcache_parent() waiting for that kill to finish.
//
// Two roles, both required:
//   spinners -- never sleep, so the autogroup entity never dequeues. This is
//               what makes the starvation permanent.
//   nappers  -- their wakeups set need_resched (cond_resched only switches when
//               it is already set) and their PELT churn stalls avg_vruntime.
//               This is what parks the dying thread in the first place.
//
// No privileges needed. Requires kernel.sched_autogroup_enabled=1.
//
//   gcc -O2 -Wall -pthread -o mini waitpid_livelock_mini.c
//   ./mini [rounds] [threads] [cpu]      # same argument order as the full one
//
// It prints round numbers, then stops: the last waitpid never returned. The
// reaper is now burning a full core in kernel mode -- watch it with
// `top -H -p <pid>` or `awk '{print $15}' /proc/<pid>/stat` twice a second.
// Ctrl-C still works, but only because it kills the load process too: with no
// always-runnable neighbour left the ghost is picked at once and the reaper
// unwinds. That is the whole bug in one keystroke.

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SPINNERS 2
#define NAPPERS  6

struct duty { long sleep_ns, work_us; };

static int target_cpu = 3;
static struct duty napper_duty = {  100000, 3 };    // ~10k wakeups/s each
static struct duty waker_duty  = { 1000000, 100 };  // a coarser monitor thread

static int pin(void) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(target_cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set);
}

static void busy_us(long us) {
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    do {
        clock_gettime(CLOCK_MONOTONIC, &b);
    } while ((b.tv_sec - a.tv_sec) * 1000000000L + b.tv_nsec - a.tv_nsec < us * 1000);
}

static void *spinner(void *arg) {
    (void)arg;
    pin();
    for (;;) sched_yield();
    return NULL;
}

static void *napper(void *arg) {
    struct duty *d = arg;
    struct timespec t = { 0, d->sleep_ns };
    pin();
    for (;;) {
        nanosleep(&t, NULL);
        busy_us(d->work_us);
    }
    return NULL;
}

static void *idler(void *arg) {
    (void)arg;
    pin();          // so the exit-path flush runs on the target CPU
    pause();
    return NULL;
}

// What any thread-walking monitor does: creates a dentry per thread, which the
// exit path will then have to tear down.
static void warm_dentries(pid_t pid) {
    char dir[64], path[512], buf[512];
    snprintf(dir, sizeof(dir), "/proc/%d/task", pid);
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s/stat", dir, e->d_name);
        FILE *f = fopen(path, "r");
        if (f) {
            (void)!fread(buf, 1, sizeof(buf), f);
            fclose(f);
        }
    }
    closedir(d);
}

int main(int argc, char **argv) {
    long rounds = argc > 1 ? atol(argv[1]) : 100000;
    int threads = argc > 2 ? atoi(argv[2]) : 8;
    if (argc > 3) target_cpu = atoi(argv[3]);

    cpu_set_t others;            // whatever we are allowed to run on
    sched_getaffinity(0, sizeof(others), &others);
    if (pin() != 0) {            // a bogus cpu number fails right here
        fprintf(stderr, "cpu %d: %s\n", target_cpu, strerror(errno));
        return 2;
    }

    if (fork() == 0) {           // load, in its own process so it outlives the hang
        prctl(PR_SET_PDEATHSIG, SIGKILL);   // never outlive the demo itself
        pthread_t t;
        for (int i = 0; i < SPINNERS; i++) pthread_create(&t, NULL, spinner, NULL);
        for (int i = 0; i < NAPPERS;  i++) pthread_create(&t, NULL, napper, &napper_duty);
        pthread_create(&t, NULL, napper, &waker_duty);
        for (;;) pause();
    }

    // The reaper stays off the target CPU: it must keep running to spin.
    CPU_CLR(target_cpu, &others);
    sched_setaffinity(0, sizeof(others), &others);

    for (long round = 1; round <= rounds; round++) {
        int ready[2], go[2];
        if (pipe(ready) || pipe(go)) return 1;

        pid_t child = fork();
        if (child == 0) {
            close(ready[0]); close(go[1]);
            pthread_t t;
            for (int i = 0; i < threads; i++) pthread_create(&t, NULL, idler, NULL);
            (void)!write(ready[1], "r", 1);
            char c;
            (void)!read(go[0], &c, 1);
            _exit(0);            // exit_group: every thread flushes /proc at once
        }
        close(ready[1]); close(go[0]);

        char c;
        (void)!read(ready[0], &c, 1);   // threads exist
        warm_dentries(child);
        (void)!write(go[1], "g", 1);    // exit now, with the cache hot

        busy_us(500);            // head start: let the threads unhash first

        printf("round %ld\n", round);
        fflush(stdout);
        while (waitpid(child, NULL, WNOHANG) == 0)   // must never block. it does.
            usleep(1000);

        close(ready[0]); close(go[1]);
    }
}
