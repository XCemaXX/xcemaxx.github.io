// waitpid(WNOHANG) that never returns: two Linux kernel defects meeting.
//
// WNOHANG has exactly one contract: never block. On EEVDF-era kernels (6.6 and
// up; seen on 6.14) a parent reaping a multi-threaded zombie can stay inside
// that syscall forever at 100% system time. SIGKILL does not touch it. The only
// way out is removing an unrelated CPU hog from another core.
//
// DEFECT 1 (VFS). release_task() drops the /proc entries of the reaped process:
//   waitpid -> wait_task_zombie -> release_task -> proc_flush_pid
//           -> proc_invalidate_siblings_dcache -> d_invalidate
//           -> shrink_dcache_parent
// shrink_dcache_parent() retries in an unbounded for(;;). A dentry that is
// already dying (d_count < 0) gives that loop no progress path, so it rescans
// forever -- no sleep, no signal checks. Fixed upstream only in v7.1 (Al Viro,
// "getting rid of busy-wait in shrink_dcache_parent"), no stable backports, and
// the fix only puts the spinner to sleep: waitpid still would not return.
//
// DEFECT 2 (scheduler). Somebody has to leave a dentry half-killed, and that is
// a thread of the child: on its way out it flushes its own /proc entries
// through the same chain and hits cond_resched() inside __dentry_kill(). It
// yields the CPU while still RUNNABLE and is never picked again. do_exit() has
// already called sched_autogroup_exit_task(), so it now sits alone in the ROOT
// task group, where re-placement leaves it ineligible by a few ms of vruntime
// debt. Its only rival there is the autogroup's group entity, kept permanently
// runnable by the sched_yield spinners below, and unfixed EEVDF bugs (reweight
// dragging avg_vruntime backwards, min/avg vruntime drift; both fixed only in
// ~6.19/tip) stop avg_vruntime from ever catching up. So the two threads
// collide: one parked mid-kill, the other spinning over the same subtree
// waiting for that kill to finish.
//
// INGREDIENTS, two roles that are easy to confuse:
//   * ENTRY -- parkers and the waker. cond_resched() only switches when
//     need_resched is already set, and pure spinners never set it. Short
//     sleep/run cycles supply the wakeup preemption that parks the dying
//     thread, and their PELT on/off swing drives the reweight churn that
//     stalls avg_vruntime. Without them nothing wedges at all.
//   * PERSISTENCE -- the sched_yield spinners. They keep the autogroup entity
//     runnable so it never dequeues. Boolean, not a matter of degree: the
//     moment that autogroup has nothing runnable on the CPU, the parked thread
//     is the only entity left at root level and gets picked regardless of
//     eligibility, finishing its kill in microseconds. Parkers alone cannot
//     hold the wedge -- each is idle ~97% of the time, so the queue keeps
//     draining. One always-runnable neighbour is what turns a routine
//     preemption into forever.
// Entry is harmless by itself: that cond_resched is crossed thousands of times
// per second and virtually always returns.
//
// BUILD / RUN
//   gcc -O2 -Wall -pthread -o waitpid_livelock waitpid_livelock.c
//   ./waitpid_livelock [rounds] [threads] [cpu] [--spinners=N] [--parkers=N]
//                      [--wakers=N] [--delay-us=N] [--auto-release=SEC] [--hold]
// A run is self-contained: it stops at the first wedge, releases it a few
// seconds later (--auto-release=SEC) and exits. Exit code 0 means reproduced,
// 1 means clean, so the discrimination runs below are scriptable. --hold keeps
// the wedge until you send the kill yourself -- use it to capture the state.
// No privileges needed (sched_yield is unprivileged), root only for the debugfs
// captures. Requires kernel.sched_autogroup_enabled=1, checked at startup;
// turning it off is one of the mitigations. Everything runs in one session
// hence one autogroup, like a service sharing a session with its supervisor;
// the trap only needs the dying thread expelled to the root group, which exit
// does by itself.
//
// EXPECT a wedge within the first few hundred rounds, under a second of
// testing. Which round is chance. The trap needs some accumulated runqueue
// state, and that builds up in seconds: a run staying clean for tens of
// thousands of rounds means a missing ingredient, not insufficient patience.
//
// WHEN IT WEDGES, capture as root -- pass --hold first, 10s is not enough:
//   awk '{print $15}' /proc/<reaper tid>/task/<reaper tid>/stat   # stime grows
//   cat /sys/kernel/debug/sched/debug > wedged.txt
// The parked thread has no line of its own anywhere -- it was unhashed from
// every pid list before starting the flush, which is why ps, /proc and sysrq-t
// all show nothing. In wedged.txt it is visible only as arithmetic over the two
// blocks of the target CPU:
//   cfs_rq[N]:/ .load  minus  cfs_rq[N]:/autogroup-* .se->load.weight
//   == NICE_0_LOAD (0x10_0000 on 64-bit: sched_prio_to_weight[nice 0] = 1024,
//                   scaled up by 1 << SCHED_FIXEDPOINT_SHIFT)
//   i.e. exactly one ordinary nice-0 task nobody lists: the ghost
// Its coordinate is the root block's .left_vruntime, frozen a few ms above
// .avg_vruntime. Sample a few times: left_vruntime does not move at all while
// avg_vruntime crawls, sometimes backwards.
//
// RELEASE is a SIGSTOP on the load process, sent automatically or by you under
// --hold. The autogroup entity dequeues, the ghost is picked within
// milliseconds, finishes its kill, and the reaper unwinds. Until something
// dequeues that entity the reaper cannot even be killed.
//
// DISCRIMINATION -- give these a large round count; each stays clean over 100k
// rounds and exits 1, three orders of magnitude past the baseline:
//   --spinners=0                                  (nothing always-runnable)
//   sysctl -w kernel.sched_autogroup_enabled=0    (no group entity to stall)
//   echo NO_PLACE_LAG > /sys/kernel/debug/sched/features   (no lag carried
//                                                           over on placement)

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define POLL_INTERVAL_NS  1000000L   // supervisor poll cadence between WNOHANG
#define STALL_REPORT_SEC  5

static atomic_long g_poll_start_ns;   // 0 = not polling
static atomic_int  g_child_pid;
static atomic_long g_round;           // round currently being executed
static atomic_int  g_wedged;
static atomic_int  g_load_pid;
static pid_t g_reaper_tid;
static long  g_hz = 100;              // clock ticks per second, from sysconf

static int  g_cpu       = 3;
static long g_delay_us  = 500;        // head start: threads must unhash first
static int  g_nspin     = 2;
static int  g_nparkers  = 6;
static int  g_nwakers   = 1;
static long g_auto_release_sec = 10;  // 0 (--hold) = wedge stays until you act

static long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

static void sleep_ns(long ns) {
    struct timespec ts = { .tv_sec = ns / 1000000000L, .tv_nsec = ns % 1000000000L };
    nanosleep(&ts, NULL);
}

static void spin_us(long us) {
    long deadline = now_ns() + us * 1000;
    while (now_ns() < deadline) {
        __asm__ __volatile__("" ::: "memory");
    }
}

static int pin_to(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set);
}

// Field 15 of .../stat: kernel-mode ticks. comm can contain spaces and parens,
// so parse after the last ')'.
static unsigned long read_stime(pid_t pid, pid_t tid) {
    char path[128], buf[1024];
    snprintf(path, sizeof(path), "/proc/%d/task/%d/stat", pid, tid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    char *p = strrchr(buf, ')');
    if (!p) return 0;
    unsigned long utime = 0, stime = 0;
    int consumed = sscanf(p + 2,
        "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %lu %lu",
        &utime, &stime);
    return consumed == 2 ? stime : 0;
}

// PERSISTENCE: never blocks, never sleeps, so the autogroup entity never
// dequeues. yield also inflates the EEVDF deadline without bound
// (yield_task_fair does deadline += slice), which feeds the reweight churn.
static void *spinner_thread(void *arg) {
    pin_to(*(int *)arg);
    for (;;) sched_yield();
    return NULL;
}

// ENTRY: ~10k wakeups/s each. Dominant source of both the wakeup preemption
// that parks the dying thread and the reweight churn on the group entity.
static void *parker_thread(void *arg) {
    pin_to(*(int *)arg);
    for (;;) {
        sleep_ns(100000);
        spin_us(3);
    }
    return NULL;
}

// ENTRY: coarser duty cycle, models a monitoring thread on the same CPU.
static void *waker_thread(void *arg) {
    pin_to(*(int *)arg);
    for (;;) {
        sleep_ns(1000000);
        spin_us(100);
    }
    return NULL;
}

// Separate process so it survives the reaper's kernel hang and can be stopped
// independently to release the wedge. Runs from start to finish: gating it per
// round breaks the repro, because that resets the accumulated runqueue state.
static pid_t spawn_load(int cpu) {
    pid_t pid = fork();
    if (pid != 0) return pid;

    prctl(PR_SET_PDEATHSIG, SIGKILL);   // never outlive the harness
    static int cpu_arg;
    cpu_arg = cpu;
    pthread_t t;
    for (int i = 0; i < g_nspin; i++)
        if (pthread_create(&t, NULL, spinner_thread, &cpu_arg) != 0)
            perror("load: spinner");
    for (int i = 0; i < g_nparkers; i++)
        if (pthread_create(&t, NULL, parker_thread, &cpu_arg) != 0)
            perror("load: parker");
    for (int i = 0; i < g_nwakers; i++)
        if (pthread_create(&t, NULL, waker_thread, &cpu_arg) != 0)
            perror("load: waker");
    for (;;) pause();
    _exit(0);
}

// Create the per-thread /proc dentries that the exit path will have to flush.
// Any monitoring tool that walks threads (top -H, ps -L, a metrics agent) does
// this in production.
static int warm_thread_dentries(pid_t pid) {
    char dirpath[64];
    snprintf(dirpath, sizeof(dirpath), "/proc/%d/task", pid);
    DIR *d = opendir(dirpath);
    if (!d) return 0;
    int seen = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s/stat", dirpath, e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            char buf[512];
            (void)!read(fd, buf, sizeof(buf));
            close(fd);
            seen++;
        }
    }
    closedir(d);
    return seen;
}

static void *idle_thread(void *arg) {
    pin_to(*(int *)arg);   // exit-path flush must run on the target CPU
    pause();
    return NULL;
}

// The leader stays off the target CPU: it has to keep running to finish
// exit_group and become a reapable zombie while its siblings are parked.
static void child_main(int nthreads, int rfd, int wfd, int cpu) {
    static int cpu_arg;
    cpu_arg = cpu;
    pthread_t t;
    for (int i = 0; i < nthreads; i++)
        if (pthread_create(&t, NULL, idle_thread, &cpu_arg) != 0) break;
    char c = 'r';
    (void)!write(wfd, &c, 1);
    (void)!read(rfd, &c, 1);   // parent warmed the cache
    _exit(0);                  // exit_group: all threads unwind at once
}

#define REPORT_NONE     0
#define REPORT_WEDGE    1
#define REPORT_FALSEPOS 2

static void *watchdog(void *arg) {
    (void)arg;
    int reported = REPORT_NONE;
    long last_stalled = 0;
    unsigned long prev_stime = 0;
    for (;;) {
        sleep(1);
        long start = atomic_load(&g_poll_start_ns);
        if (start == 0) {
            if (reported == REPORT_WEDGE)
                fprintf(stderr, "*** released after %lds, waitpid returned\n",
                        last_stalled);
            reported = REPORT_NONE;
            prev_stime = 0;
            continue;
        }
        long stalled = (now_ns() - start) / 1000000000L;
        last_stalled = stalled;
        unsigned long stime = read_stime(g_reaper_tid, g_reaper_tid);
        if (stalled < STALL_REPORT_SEC) { prev_stime = stime; continue; }
        // The wedge burns a full core in the kernel. Flat stime instead means
        // the child never became a zombie -- its leader got starved too, so
        // raise --delay-us.
        int spinning = prev_stime && stime > prev_stime + g_hz / 2;
        prev_stime = stime;
        if (reported == REPORT_NONE) {
            reported = spinning ? REPORT_WEDGE : REPORT_FALSEPOS;
            if (spinning) atomic_store(&g_wedged, 1);
            fprintf(stderr, "\n*** %s\n", spinning
                ? "LIVELOCK: waitpid(WNOHANG) is not returning"
                : "FALSE POSITIVE: reaper idle (child never became a zombie)"
                  " -- raise --delay-us so all threads unhash before the load"
                  " takes the CPU");
            fprintf(stderr, "    stuck %lds on child %d (round %ld)\n",
                    stalled, atomic_load(&g_child_pid),
                    atomic_load(&g_round));
            fprintf(stderr, "    reaper tid=%d target cpu=%d load pid=%d\n",
                    g_reaper_tid, g_cpu, atomic_load(&g_load_pid));
            fprintf(stderr,
                "    capture (root):\n"
                "      awk '{print $15}' /proc/%d/task/%d/stat\n"
                "      cat /sys/kernel/debug/sched/debug > wedged.txt\n"
                "        # in wedged.txt, for cpu %d:\n"
                "        #   cfs_rq[%d]:/ .load  minus\n"
                "        #   cfs_rq[%d]:/autogroup-* .se->load.weight  == 1048576\n"
                "        #   root .left_vruntime frozen above .avg_vruntime\n",
                g_reaper_tid, g_reaper_tid, g_cpu, g_cpu, g_cpu);
            if (g_auto_release_sec > 0)
                fprintf(stderr, "    auto-release in %lds; --hold keeps it "
                        "wedged until you send kill -STOP %d yourself\n",
                        g_auto_release_sec, atomic_load(&g_load_pid));
            else
                fprintf(stderr, "    release: kill -STOP %d   (the run ends "
                        "once waitpid unwinds)\n", atomic_load(&g_load_pid));
        } else if (reported == REPORT_FALSEPOS && spinning) {
            reported = REPORT_WEDGE;   // zombie appeared late, then wedged
            atomic_store(&g_wedged, 1);
            fprintf(stderr, "*** LIVELOCK after all: reaper started spinning\n");
        }
        fprintf(stderr, "    stuck %lds, reaper stime=%lu ticks (%s)\n",
                stalled, stime, spinning ? "spinning in kernel" : "idle");

        if (g_auto_release_sec > 0 && reported == REPORT_WEDGE
            && stalled >= g_auto_release_sec) {
            int lp = atomic_load(&g_load_pid);
            fprintf(stderr, "    auto-release: STOP/CONT on %d\n", lp);
            kill(lp, SIGSTOP);
            sleep(1);
            kill(lp, SIGCONT);
        }
    }
    return NULL;
}

// Printed at startup so a discrimination transcript proves which toggle was
// active. Needs root to read debugfs.
static const char *place_lag_state(void) {
    int fd = open("/sys/kernel/debug/sched/features", O_RDONLY);
    if (fd < 0) return "unreadable";
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return "unreadable";
    buf[n] = '\0';
    return strstr(buf, "NO_PLACE_LAG") ? "off" : "on";
}

static int autogroup_enabled(void) {
    int fd = open("/proc/sys/kernel/sched_autogroup_enabled", O_RDONLY);
    if (fd < 0) return -1;
    char c = '0';
    (void)!read(fd, &c, 1);
    close(fd);
    return c == '1';
}

int main(int argc, char **argv) {
    long rounds  = argc > 1 && argv[1][0] != '-' ? atol(argv[1]) : 100000;
    int  nthread = argc > 2 && argv[2][0] != '-' ? atoi(argv[2]) : 8;
    if (argc > 3 && argv[3][0] != '-') g_cpu = atoi(argv[3]);
    for (int i = 1; i < argc; i++) {
        if      (!strncmp(argv[i], "--spinners=", 11)) g_nspin    = atoi(argv[i] + 11);
        else if (!strncmp(argv[i], "--parkers=",  10)) g_nparkers = atoi(argv[i] + 10);
        else if (!strncmp(argv[i], "--wakers=",    9)) g_nwakers  = atoi(argv[i] + 9);
        else if (!strncmp(argv[i], "--delay-us=", 11)) g_delay_us = atol(argv[i] + 11);
        else if (!strncmp(argv[i], "--auto-release=", 15)) g_auto_release_sec = atol(argv[i] + 15);
        else if (!strcmp(argv[i], "--hold")) g_auto_release_sec = 0;
    }

    cpu_set_t others;            // whatever we are allowed to run on
    sched_getaffinity(0, sizeof(others), &others);
    if (pin_to(g_cpu) != 0) {    // a bogus cpu number fails right here
        fprintf(stderr, "cpu %d: %s\n", g_cpu, strerror(errno));
        return 2;
    }

    g_hz = sysconf(_SC_CLK_TCK);
    if (g_hz <= 0) g_hz = 100;
    g_reaper_tid = (pid_t)gettid();
    printf("rounds=%ld threads=%d cpu=%d spinners=%d parkers=%d wakers=%d "
           "delay=%ldus reaper_tid=%d hz=%ld\n",
           rounds, nthread, g_cpu, g_nspin, g_nparkers, g_nwakers,
           g_delay_us, g_reaper_tid, g_hz);

    int ag = autogroup_enabled();
    printf("sched: autogroup=%s PLACE_LAG=%s\n",
           ag < 0 ? "unreadable" : (ag ? "1" : "0"), place_lag_state());
    if (ag == 0)
        printf("WARNING: sched_autogroup_enabled=0 -- the trap cannot form, "
               "expect no wedge (that is the mitigation)\n");
    else if (ag < 0)
        printf("WARNING: cannot read sched_autogroup_enabled\n");

    pid_t load = spawn_load(g_cpu);
    atomic_store(&g_load_pid, load);
    printf("load pid=%d on cpu %d\n", load, g_cpu);

    pthread_t wd;
    pthread_create(&wd, NULL, watchdog, NULL);

    // The reaper must stay off the target CPU: it has to keep running in order
    // to spin in shrink_dcache_parent while the child's thread is parked.
    CPU_CLR(g_cpu, &others);
    if (sched_setaffinity(0, sizeof(others), &others) != 0)
        perror("reaper affinity");

    for (long r = 0; r < rounds; r++) {
        atomic_store(&g_round, r + 1);
        int up[2], down[2];
        if (pipe(up) || pipe(down)) { perror("pipe"); break; }

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); break; }
        if (pid == 0) {
            close(up[0]); close(down[1]);
            child_main(nthread, down[0], up[1], g_cpu);
        }
        close(up[1]); close(down[0]);

        char c;
        if (read(up[0], &c, 1) != 1) { /* child died early */ }
        int warmed = warm_thread_dentries(pid);
        (void)!write(down[1], &c, 1);   // let the child exit_group

        // Head start so the exit-path flush is under way before the first poll.
        spin_us(g_delay_us);

        atomic_store(&g_child_pid, pid);
        atomic_store(&g_poll_start_ns, now_ns());
        int status = 0;
        for (;;) {
            // Contractually non-blocking. This is the call that never returns.
            pid_t got = waitpid(pid, &status, WNOHANG);
            if (got == pid) break;
            if (got < 0 && errno != EINTR) { perror("waitpid"); break; }
            sleep_ns(POLL_INTERVAL_NS);
        }
        atomic_store(&g_poll_start_ns, 0);

        close(up[0]); close(down[1]);

        // Once it has wedged the point is made. Further rounds prove nothing,
        // and after a STOP/CONT release the accumulated runqueue state is gone,
        // so a clean tail would be misleading.
        if (atomic_load(&g_wedged)) break;

        if ((r + 1) % 100 == 0) {
            printf("round %ld ok (warmed %d thread dentries)\n", r + 1, warmed);
            fflush(stdout);
        }
    }

    int  wedged = atomic_load(&g_wedged);
    long done   = atomic_load(&g_round);
    printf(wedged ? "REPRODUCED: livelock at round %ld\n"
                  : "clean: no livelock in %ld rounds\n", done);
    kill(load, SIGKILL);
    return wedged ? 0 : 1;
}
