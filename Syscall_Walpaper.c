/*
 * syscall_wrapper.c - System Call Wrappers
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * User space wrappers para todas las syscalls del kernel.
 * Interface entre user space y kernel mode.
 * 
 * ARM64 System Call Convention:
 * - Syscall number: X8
 * - Arguments: X0-X5 (hasta 6 args)
 * - Return value: X0
 * - Instruction: SVC #0
 * 
 * PRODUCCIÓN - BARE-METAL - ZERO DEPENDENCIES
 */

#include "types.h"

/* Syscall numbers */
#define SYS_EXIT                0
#define SYS_FORK                1
#define SYS_READ                2
#define SYS_WRITE               3
#define SYS_OPEN                4
#define SYS_CLOSE               5
#define SYS_WAITPID             6
#define SYS_CREAT               7
#define SYS_LINK                8
#define SYS_UNLINK              9
#define SYS_EXECVE              10
#define SYS_CHDIR               11
#define SYS_TIME                12
#define SYS_MKNOD               13
#define SYS_CHMOD               14
#define SYS_LSEEK               15
#define SYS_GETPID              16
#define SYS_MOUNT               17
#define SYS_UMOUNT              18
#define SYS_SETUID              19
#define SYS_GETUID              20
#define SYS_STIME               21
#define SYS_PTRACE              22
#define SYS_ALARM               23
#define SYS_PAUSE               24
#define SYS_ACCESS              25
#define SYS_KILL                26
#define SYS_RENAME              27
#define SYS_MKDIR               28
#define SYS_RMDIR               29
#define SYS_DUP                 30
#define SYS_PIPE                31
#define SYS_BRK                 32
#define SYS_IOCTL               33
#define SYS_FCNTL               34
#define SYS_SETPGID             35
#define SYS_UMASK               36
#define SYS_CHROOT              37
#define SYS_DUP2                38
#define SYS_GETPPID             39
#define SYS_GETPGRP             40
#define SYS_SETSID              41
#define SYS_SIGACTION           42
#define SYS_SETREUID            43
#define SYS_SETREGID            44
#define SYS_TIMES               45
#define SYS_GETEUID             46
#define SYS_GETEGID             47
#define SYS_SETPGID2            48
#define SYS_SETUID2             49
#define SYS_SETGID              50

/* Memory management */
#define SYS_MMAP                51
#define SYS_MUNMAP              52
#define SYS_MPROTECT            53
#define SYS_MSYNC               54
#define SYS_MLOCK               55
#define SYS_MUNLOCK             56
#define SYS_MLOCKALL            57
#define SYS_MUNLOCKALL          58

/* Process/Thread management */
#define SYS_CLONE               59
#define SYS_EXECVEAT            60
#define SYS_WAIT4               61
#define SYS_VFORK               62
#define SYS_SCHED_YIELD         63
#define SYS_SCHED_SETPARAM      64
#define SYS_SCHED_GETPARAM      65
#define SYS_SCHED_SETSCHEDULER  66
#define SYS_SCHED_GETSCHEDULER  67
#define SYS_SCHED_GET_PRIORITY_MAX  68
#define SYS_SCHED_GET_PRIORITY_MIN  69
#define SYS_NANOSLEEP           70

/* File system */
#define SYS_STAT                71
#define SYS_FSTAT               72
#define SYS_LSTAT               73
#define SYS_POLL                74
#define SYS_READV               75
#define SYS_WRITEV              76
#define SYS_SELECT              77
#define SYS_FSYNC               78
#define SYS_FDATASYNC           79
#define SYS_TRUNCATE            80
#define SYS_FTRUNCATE           81
#define SYS_GETDENTS            82
#define SYS_GETCWD              83
#define SYS_CHOWN               84
#define SYS_FCHOWN              85
#define SYS_LCHOWN              86
#define SYS_SYMLINK             87
#define SYS_READLINK            88

/* Signals */
#define SYS_SIGRETURN           89
#define SYS_RT_SIGACTION        90
#define SYS_RT_SIGPROCMASK      91
#define SYS_RT_SIGPENDING       92
#define SYS_RT_SIGTIMEDWAIT     93
#define SYS_RT_SIGQUEUEINFO     94
#define SYS_RT_SIGSUSPEND       95

/* IPC */
#define SYS_MSGGET              96
#define SYS_MSGSND              97
#define SYS_MSGRCV              98
#define SYS_MSGCTL              99
#define SYS_SEMGET              100
#define SYS_SEMOP               101
#define SYS_SEMCTL              102
#define SYS_SHMGET              103
#define SYS_SHMAT               104
#define SYS_SHMDT               105
#define SYS_SHMCTL              106

/* Socket */
#define SYS_SOCKET              107
#define SYS_BIND                108
#define SYS_CONNECT             109
#define SYS_LISTEN              110
#define SYS_ACCEPT              111
#define SYS_GETSOCKNAME         112
#define SYS_GETPEERNAME         113
#define SYS_SOCKETPAIR          114
#define SYS_SEND                115
#define SYS_RECV                116
#define SYS_SENDTO              117
#define SYS_RECVFROM            118
#define SYS_SHUTDOWN            119
#define SYS_SETSOCKOPT          120
#define SYS_GETSOCKOPT          121
#define SYS_SENDMSG             122
#define SYS_RECVMSG             123

/* Time */
#define SYS_GETTIMEOFDAY        124
#define SYS_SETTIMEOFDAY        125
#define SYS_CLOCK_GETTIME       126
#define SYS_CLOCK_SETTIME       127
#define SYS_CLOCK_GETRES        128
#define SYS_CLOCK_NANOSLEEP     129

/* Advanced file ops */
#define SYS_PREAD64             130
#define SYS_PWRITE64            131
#define SYS_PREADV              132
#define SYS_PWRITEV             133
#define SYS_SENDFILE            134
#define SYS_SPLICE              135
#define SYS_TEE                 136
#define SYS_SYNC_FILE_RANGE     137

/* Extended attributes */
#define SYS_SETXATTR            138
#define SYS_LSETXATTR           139
#define SYS_FSETXATTR           140
#define SYS_GETXATTR            141
#define SYS_LGETXATTR           142
#define SYS_FGETXATTR           143
#define SYS_LISTXATTR           144
#define SYS_LLISTXATTR          145
#define SYS_FLISTXATTR          146
#define SYS_REMOVEXATTR         147
#define SYS_LREMOVEXATTR        148
#define SYS_FREMOVEXATTR        149

/* Advanced process management */
#define SYS_FUTEX               150
#define SYS_SET_TID_ADDRESS     151
#define SYS_GET_THREAD_AREA     152
#define SYS_SET_THREAD_AREA     153
#define SYS_CAPGET              154
#define SYS_CAPSET              155
#define SYS_PRCTL               156

/* System info */
#define SYS_UNAME               157
#define SYS_SYSINFO             158
#define SYS_GETRUSAGE           159
#define SYS_SYSLOG              160
#define SYS_REBOOT              161

/* Epoll */
#define SYS_EPOLL_CREATE        162
#define SYS_EPOLL_CTL           163
#define SYS_EPOLL_WAIT          164
#define SYS_EPOLL_PWAIT         165

/* Advanced I/O */
#define SYS_INOTIFY_INIT        166
#define SYS_INOTIFY_ADD_WATCH   167
#define SYS_INOTIFY_RM_WATCH    168
#define SYS_FANOTIFY_INIT       169
#define SYS_FANOTIFY_MARK       170

/* Timer */
#define SYS_TIMER_CREATE        171
#define SYS_TIMER_SETTIME       172
#define SYS_TIMER_GETTIME       173
#define SYS_TIMER_GETOVERRUN    174
#define SYS_TIMER_DELETE        175
#define SYS_TIMERFD_CREATE      176
#define SYS_TIMERFD_SETTIME     177
#define SYS_TIMERFD_GETTIME     178

/* Eventfd */
#define SYS_EVENTFD             179
#define SYS_SIGNALFD            180

/* Performance monitoring */
#define SYS_PERF_EVENT_OPEN     181

/* Filesystem operations */
#define SYS_OPENAT              182
#define SYS_MKDIRAT             183
#define SYS_MKNODAT             184
#define SYS_FCHOWNAT            185
#define SYS_FUTIMESAT           186
#define SYS_NEWFSTATAT          187
#define SYS_UNLINKAT            188
#define SYS_RENAMEAT            189
#define SYS_LINKAT              190
#define SYS_SYMLINKAT           191
#define SYS_READLINKAT          192
#define SYS_FCHMODAT            193
#define SYS_FACCESSAT           194

/* Process credentials */
#define SYS_GETGROUPS           195
#define SYS_SETGROUPS           196
#define SYS_GETRESUID           197
#define SYS_SETRESUID           198
#define SYS_GETRESGID           199
#define SYS_SETRESGID           200

/* Visor OS specific syscalls */
#define SYS_VISOR_GET_MODE      240
#define SYS_VISOR_SET_MODE      241
#define SYS_VISOR_GET_HW_INFO   242
#define SYS_VISOR_XR_TRACKING   243
#define SYS_VISOR_COMPOSITOR    244
#define SYS_VISOR_DEBUG         245

/* Error codes */
#define EPERM           1
#define ENOENT          2
#define ESRCH           3
#define EINTR           4
#define EIO             5
#define ENXIO           6
#define E2BIG           7
#define ENOEXEC         8
#define EBADF           9
#define ECHILD          10
#define EAGAIN          11
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define EBUSY           16
#define EEXIST          17
#define EXDEV           18
#define ENODEV          19
#define ENOTDIR         20
#define EISDIR          21
#define EINVAL          22
#define ENFILE          23
#define EMFILE          24
#define ENOTTY          25
#define ETXTBSY         26
#define EFBIG           27
#define ENOSPC          28
#define ESPIPE          29
#define EROFS           30
#define EMLINK          31
#define EPIPE           32
#define EDOM            33
#define ERANGE          34

/* Global errno */
static int _errno = 0;
int *__errno_location(void) { return &_errno; }
#define errno (*__errno_location())

/* ARM64 syscall with 0 arguments */
static inline long syscall0(long number)
{
    register long x8 __asm__("x8") = number;
    register long x0 __asm__("x0");
    
    __asm__ volatile(
        "svc #0"
        : "=r"(x0)
        : "r"(x8)
        : "memory"
    );
    
    if (x0 < 0) {
        errno = -x0;
        return -1;
    }
    
    return x0;
}

/* ARM64 syscall with 1 argument */
static inline long syscall1(long number, long arg1)
{
    register long x8 __asm__("x8") = number;
    register long x0 __asm__("x0") = arg1;
    
    __asm__ volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8)
        : "memory"
    );
    
    if (x0 < 0) {
        errno = -x0;
        return -1;
    }
    
    return x0;
}

/* ARM64 syscall with 2 arguments */
static inline long syscall2(long number, long arg1, long arg2)
{
    register long x8 __asm__("x8") = number;
    register long x0 __asm__("x0") = arg1;
    register long x1 __asm__("x1") = arg2;
    
    __asm__ volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1)
        : "memory"
    );
    
    if (x0 < 0) {
        errno = -x0;
        return -1;
    }
    
    return x0;
}

/* ARM64 syscall with 3 arguments */
static inline long syscall3(long number, long arg1, long arg2, long arg3)
{
    register long x8 __asm__("x8") = number;
    register long x0 __asm__("x0") = arg1;
    register long x1 __asm__("x1") = arg2;
    register long x2 __asm__("x2") = arg3;
    
    __asm__ volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2)
        : "memory"
    );
    
    if (x0 < 0) {
        errno = -x0;
        return -1;
    }
    
    return x0;
}

/* ARM64 syscall with 4 arguments */
static inline long syscall4(long number, long arg1, long arg2, long arg3, long arg4)
{
    register long x8 __asm__("x8") = number;
    register long x0 __asm__("x0") = arg1;
    register long x1 __asm__("x1") = arg2;
    register long x2 __asm__("x2") = arg3;
    register long x3 __asm__("x3") = arg4;
    
    __asm__ volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3)
        : "memory"
    );
    
    if (x0 < 0) {
        errno = -x0;
        return -1;
    }
    
    return x0;
}

/* ARM64 syscall with 5 arguments */
static inline long syscall5(long number, long arg1, long arg2, long arg3, long arg4, long arg5)
{
    register long x8 __asm__("x8") = number;
    register long x0 __asm__("x0") = arg1;
    register long x1 __asm__("x1") = arg2;
    register long x2 __asm__("x2") = arg3;
    register long x3 __asm__("x3") = arg4;
    register long x4 __asm__("x4") = arg5;
    
    __asm__ volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4)
        : "memory"
    );
    
    if (x0 < 0) {
        errno = -x0;
        return -1;
    }
    
    return x0;
}

/* ARM64 syscall with 6 arguments */
static inline long syscall6(long number, long arg1, long arg2, long arg3, long arg4, long arg5, long arg6)
{
    register long x8 __asm__("x8") = number;
    register long x0 __asm__("x0") = arg1;
    register long x1 __asm__("x1") = arg2;
    register long x2 __asm__("x2") = arg3;
    register long x3 __asm__("x3") = arg4;
    register long x4 __asm__("x4") = arg5;
    register long x5 __asm__("x5") = arg6;
    
    __asm__ volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
        : "memory"
    );
    
    if (x0 < 0) {
        errno = -x0;
        return -1;
    }
    
    return x0;
}

/* ========== Process Management ========== */

void exit(int status)
{
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

int fork(void)
{
    return syscall0(SYS_FORK);
}

int getpid(void)
{
    return syscall0(SYS_GETPID);
}

int getppid(void)
{
    return syscall0(SYS_GETPPID);
}

int execve(const char *filename, char *const argv[], char *const envp[])
{
    return syscall3(SYS_EXECVE, (long)filename, (long)argv, (long)envp);
}

int waitpid(int pid, int *status, int options)
{
    return syscall3(SYS_WAITPID, pid, (long)status, options);
}

int kill(int pid, int sig)
{
    return syscall2(SYS_KILL, pid, sig);
}

/* ========== File I/O ========== */

int open(const char *pathname, int flags, int mode)
{
    return syscall3(SYS_OPEN, (long)pathname, flags, mode);
}

int close(int fd)
{
    return syscall1(SYS_CLOSE, fd);
}

ssize_t read(int fd, void *buf, size_t count)
{
    return syscall3(SYS_READ, fd, (long)buf, count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return syscall3(SYS_WRITE, fd, (long)buf, count);
}

off_t lseek(int fd, off_t offset, int whence)
{
    return syscall3(SYS_LSEEK, fd, offset, whence);
}

int dup(int oldfd)
{
    return syscall1(SYS_DUP, oldfd);
}

int dup2(int oldfd, int newfd)
{
    return syscall2(SYS_DUP2, oldfd, newfd);
}

int pipe(int pipefd[2])
{
    return syscall1(SYS_PIPE, (long)pipefd);
}

/* ========== File System ========== */

int chdir(const char *path)
{
    return syscall1(SYS_CHDIR, (long)path);
}

int mkdir(const char *pathname, int mode)
{
    return syscall2(SYS_MKDIR, (long)pathname, mode);
}

int rmdir(const char *pathname)
{
    return syscall1(SYS_RMDIR, (long)pathname);
}

int unlink(const char *pathname)
{
    return syscall1(SYS_UNLINK, (long)pathname);
}

int link(const char *oldpath, const char *newpath)
{
    return syscall2(SYS_LINK, (long)oldpath, (long)newpath);
}

int rename(const char *oldpath, const char *newpath)
{
    return syscall2(SYS_RENAME, (long)oldpath, (long)newpath);
}

int chmod(const char *pathname, int mode)
{
    return syscall2(SYS_CHMOD, (long)pathname, mode);
}

int chown(const char *pathname, int owner, int group)
{
    return syscall3(SYS_CHOWN, (long)pathname, owner, group);
}

int access(const char *pathname, int mode)
{
    return syscall2(SYS_ACCESS, (long)pathname, mode);
}

/* ========== Memory Management ========== */

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    long ret = syscall6(SYS_MMAP, (long)addr, length, prot, flags, fd, offset);
    if (ret < 0) {
        return (void *)-1;
    }
    return (void *)ret;
}

int munmap(void *addr, size_t length)
{
    return syscall2(SYS_MUNMAP, (long)addr, length);
}

int mprotect(void *addr, size_t len, int prot)
{
    return syscall3(SYS_MPROTECT, (long)addr, len, prot);
}

void *brk(void *addr)
{
    long ret = syscall1(SYS_BRK, (long)addr);
    return (void *)ret;
}

/* ========== Time ========== */

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    return syscall2(SYS_GETTIMEOFDAY, (long)tv, (long)tz);
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    return syscall2(SYS_NANOSLEEP, (long)req, (long)rem);
}

unsigned int sleep(unsigned int seconds)
{
    struct timespec req, rem;
    req.tv_sec = seconds;
    req.tv_nsec = 0;
    
    if (nanosleep(&req, &rem) == 0) {
        return 0;
    }
    
    return rem.tv_sec;
}

/* ========== Scheduling ========== */

int sched_yield(void)
{
    return syscall0(SYS_SCHED_YIELD);
}

/* ========== Signals ========== */

typedef void (*sighandler_t)(int);

sighandler_t signal(int signum, sighandler_t handler)
{
    /* Simplified - would use sigaction internally */
    (void)signum;
    (void)handler;
    return (sighandler_t)0;
}

/* ========== I/O Control ========== */

int ioctl(int fd, unsigned long request, void *argp)
{
    return syscall3(SYS_IOCTL, fd, request, (long)argp);
}

int fcntl(int fd, int cmd, long arg)
{
    return syscall3(SYS_FCNTL, fd, cmd, arg);
}

/* ========== Socket (basic) ========== */

int socket(int domain, int type, int protocol)
{
    return syscall3(SYS_SOCKET, domain, type, protocol);
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return syscall3(SYS_BIND, sockfd, (long)addr, addrlen);
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return syscall3(SYS_CONNECT, sockfd, (long)addr, addrlen);
}

int listen(int sockfd, int backlog)
{
    return syscall2(SYS_LISTEN, sockfd, backlog);
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return syscall3(SYS_ACCEPT, sockfd, (long)addr, (long)addrlen);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    return syscall4(SYS_SEND, sockfd, (long)buf, len, flags);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    return syscall4(SYS_RECV, sockfd, (long)buf, len, flags);
}

/* ========== Visor OS Specific ========== */

int visor_get_mode(void)
{
    return syscall0(SYS_VISOR_GET_MODE);
}

int visor_set_mode(int mode)
{
    return syscall1(SYS_VISOR_SET_MODE, mode);
}

int visor_get_hw_info(void *info)
{
    return syscall1(SYS_VISOR_GET_HW_INFO, (long)info);
}

int visor_xr_tracking(int cmd, void *data)
{
    return syscall2(SYS_VISOR_XR_TRACKING, cmd, (long)data);
}

int visor_compositor(int cmd, void *data)
{
    return syscall2(SYS_VISOR_COMPOSITOR, cmd, (long)data);
}

int visor_debug(int level, const char *msg)
{
    return syscall2(SYS_VISOR_DEBUG, level, (long)msg);
}

/* ========== Utility Functions ========== */

/* Get errno */
int get_errno(void)
{
    return errno;
}

/* Set errno */
void set_errno(int err)
{
    errno = err;
}

/* Check if syscall failed */
bool syscall_failed(long ret)
{
    return ret < 0;
}

/* Get error string (simplified) */
const char *strerror(int errnum)
{
    switch (errnum) {
        case 0:         return "Success";
        case EPERM:     return "Operation not permitted";
        case ENOENT:    return "No such file or directory";
        case ESRCH:     return "No such process";
        case EINTR:     return "Interrupted system call";
        case EIO:       return "I/O error";
        case ENXIO:     return "No such device or address";
        case E2BIG:     return "Argument list too long";
        case ENOEXEC:   return "Exec format error";
        case EBADF:     return "Bad file number";
        case ECHILD:    return "No child processes";
        case EAGAIN:    return "Try again";
        case ENOMEM:    return "Out of memory";
        case EACCES:    return "Permission denied";
        case EFAULT:    return "Bad address";
        case EBUSY:     return "Device or resource busy";
        case EEXIST:    return "File exists";
        case EXDEV:     return "Cross-device link";
        case ENODEV:    return "No such device";
        case ENOTDIR:   return "Not a directory";
        case EISDIR:    return "Is a directory";
        case EINVAL:    return "Invalid argument";
        case ENFILE:    return "File table overflow";
        case EMFILE:    return "Too many open files";
        case ENOTTY:    return "Not a typewriter";
        case ETXTBSY:   return "Text file busy";
        case EFBIG:     return "File too large";
        case ENOSPC:    return "No space left on device";
        case ESPIPE:    return "Illegal seek";
        case EROFS:     return "Read-only file system";
        case EMLINK:    return "Too many links";
        case EPIPE:     return "Broken pipe";
        case EDOM:      return "Math argument out of domain";
        case ERANGE:    return "Math result not representable";
        default:        return "Unknown error";
    }
}

/* Perror - print error */
void perror(const char *s)
{
    extern void uart_puts(const char *);
    
    if (s && *s) {
        uart_puts(s);
        uart_puts(": ");
    }
    uart_puts(strerror(errno));
    uart_puts("\n");
}