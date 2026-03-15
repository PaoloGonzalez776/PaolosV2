/*
 * libvisor_core.c - Visor OS Core Library
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Biblioteca principal de Visor OS para aplicaciones userspace.
 * Provee interfaz de alto nivel a servicios del sistema.
 * 
 * CARACTERÍSTICAS:
 * - Syscall wrappers (interfaz limpia a kernel)
 * - API de servicios (audio, bluetooth, camera, etc)
 * - Gestión de memoria userspace
 * - IPC helpers
 * - Threading primitives
 * - Sincronización (mutex, semaphores)
 * 
 * ESTA ES LA BIBLIOTECA QUE USAN LAS APLICACIONES:
 * 
 * #include <libvisor.h>
 * 
 * int main() {
 *     visor_audio_play("sound.wav");
 *     visor_camera_capture();
 *     return 0;
 * }
 * 
 * PRODUCCIÓN - BARE-METAL - ZERO DEPENDENCIES
 */

#include "types.h"

/* Syscall numbers (must match kernel) */
#define SYS_EXIT                0
#define SYS_WRITE               1
#define SYS_READ                2
#define SYS_OPEN                3
#define SYS_CLOSE               4
#define SYS_FORK                5
#define SYS_EXEC                6
#define SYS_WAIT                7
#define SYS_GETPID              8
#define SYS_KILL                9
#define SYS_SLEEP               10
#define SYS_MMAP                11
#define SYS_MUNMAP              12
#define SYS_IPC_SEND            20
#define SYS_IPC_RECV            21
#define SYS_SHM_CREATE          22
#define SYS_SHM_ATTACH          23
#define SYS_AUDIO_PLAY          30
#define SYS_CAMERA_CAPTURE      31
#define SYS_BT_CONNECT          32
#define SYS_HAPTIC_VIBRATE      33

/* Standard file descriptors */
#define STDIN                   0
#define STDOUT                  1
#define STDERR                  2

/* Memory allocation constants */
#define HEAP_SIZE               (16 * 1024 * 1024)  /* 16 MB heap per process */
#define BLOCK_MAGIC             0xDEADBEEF

/* Thread constants */
#define MAX_THREADS             32
#define THREAD_STACK_SIZE       (64 * 1024)  /* 64 KB per thread */

/* Memory block header */
typedef struct mem_block {
    uint32_t magic;
    size_t size;
    bool free;
    struct mem_block *next;
} mem_block_t;

/* Thread control block */
typedef struct {
    uint32_t tid;
    void *(*start_routine)(void *);
    void *arg;
    void *stack;
    bool active;
    int exit_code;
} thread_t;

/* Mutex */
typedef struct {
    volatile uint32_t lock;
    uint32_t owner;
} visor_mutex_t;

/* Semaphore */
typedef struct {
    volatile int32_t count;
    volatile uint32_t lock;
} visor_sem_t;

/* Global state */
static uint8_t heap[HEAP_SIZE] __attribute__((aligned(16)));
static mem_block_t *heap_head = NULL;
static bool heap_initialized = false;

static thread_t threads[MAX_THREADS];
static uint32_t next_tid = 1;

/* ============================================================================
 * SYSCALL WRAPPERS (ARM64)
 * ============================================================================ */

static inline long syscall0(long n)
{
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline long syscall1(long n, long a0)
{
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a0;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline long syscall2(long n, long a0, long a1)
{
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return x0;
}

static inline long syscall3(long n, long a0, long a1, long a2)
{
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static inline long syscall4(long n, long a0, long a1, long a2, long a3)
{
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}

/* ============================================================================
 * BASIC SYSCALLS
 * ============================================================================ */

void visor_exit(int code)
{
    syscall1(SYS_EXIT, code);
    __builtin_unreachable();
}

ssize_t visor_write(int fd, const void *buf, size_t count)
{
    return syscall3(SYS_WRITE, fd, (long)buf, count);
}

ssize_t visor_read(int fd, void *buf, size_t count)
{
    return syscall3(SYS_READ, fd, (long)buf, count);
}

int visor_open(const char *path, int flags)
{
    return syscall2(SYS_OPEN, (long)path, flags);
}

int visor_close(int fd)
{
    return syscall1(SYS_CLOSE, fd);
}

pid_t visor_fork(void)
{
    return syscall0(SYS_FORK);
}

int visor_exec(const char *path, char *const argv[])
{
    return syscall2(SYS_EXEC, (long)path, (long)argv);
}

pid_t visor_wait(int *status)
{
    return syscall1(SYS_WAIT, (long)status);
}

pid_t visor_getpid(void)
{
    return syscall0(SYS_GETPID);
}

int visor_kill(pid_t pid, int sig)
{
    return syscall2(SYS_KILL, pid, sig);
}

void visor_sleep(uint32_t ms)
{
    syscall1(SYS_SLEEP, ms);
}

void *visor_mmap(void *addr, size_t length, int prot, int flags)
{
    return (void *)syscall4(SYS_MMAP, (long)addr, length, prot, flags);
}

int visor_munmap(void *addr, size_t length)
{
    return syscall2(SYS_MUNMAP, (long)addr, length);
}

/* ============================================================================
 * MEMORY MANAGEMENT (USERSPACE HEAP)
 * ============================================================================ */

static void heap_init(void)
{
    if (heap_initialized) return;
    
    heap_head = (mem_block_t *)heap;
    heap_head->magic = BLOCK_MAGIC;
    heap_head->size = HEAP_SIZE - sizeof(mem_block_t);
    heap_head->free = true;
    heap_head->next = NULL;
    
    heap_initialized = true;
}

void *visor_malloc(size_t size)
{
    if (!heap_initialized) heap_init();
    
    if (size == 0) return NULL;
    
    /* Align to 16 bytes */
    size = (size + 15) & ~15;
    
    mem_block_t *current = heap_head;
    
    /* First-fit allocation */
    while (current) {
        if (current->magic != BLOCK_MAGIC) {
            /* Heap corruption */
            return NULL;
        }
        
        if (current->free && current->size >= size) {
            /* Split block if large enough */
            if (current->size >= size + sizeof(mem_block_t) + 16) {
                mem_block_t *new_block = (mem_block_t *)((uint8_t *)current + sizeof(mem_block_t) + size);
                new_block->magic = BLOCK_MAGIC;
                new_block->size = current->size - size - sizeof(mem_block_t);
                new_block->free = true;
                new_block->next = current->next;
                
                current->size = size;
                current->next = new_block;
            }
            
            current->free = false;
            return (void *)((uint8_t *)current + sizeof(mem_block_t));
        }
        
        current = current->next;
    }
    
    /* Out of memory */
    return NULL;
}

void visor_free(void *ptr)
{
    if (!ptr) return;
    
    mem_block_t *block = (mem_block_t *)((uint8_t *)ptr - sizeof(mem_block_t));
    
    if (block->magic != BLOCK_MAGIC) {
        /* Invalid pointer */
        return;
    }
    
    block->free = true;
    
    /* Coalesce with next block if free */
    if (block->next && block->next->free) {
        block->size += sizeof(mem_block_t) + block->next->size;
        block->next = block->next->next;
    }
    
    /* Coalesce with previous block if free */
    mem_block_t *current = heap_head;
    while (current && current->next != block) {
        current = current->next;
    }
    
    if (current && current->free) {
        current->size += sizeof(mem_block_t) + block->size;
        current->next = block->next;
    }
}

void *visor_realloc(void *ptr, size_t size)
{
    if (!ptr) return visor_malloc(size);
    if (size == 0) {
        visor_free(ptr);
        return NULL;
    }
    
    mem_block_t *block = (mem_block_t *)((uint8_t *)ptr - sizeof(mem_block_t));
    
    if (block->magic != BLOCK_MAGIC) return NULL;
    
    if (block->size >= size) {
        /* Current block is large enough */
        return ptr;
    }
    
    /* Allocate new block and copy */
    void *new_ptr = visor_malloc(size);
    if (!new_ptr) return NULL;
    
    /* Copy old data */
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (size_t i = 0; i < block->size && i < size; i++) {
        dst[i] = src[i];
    }
    
    visor_free(ptr);
    return new_ptr;
}

/* ============================================================================
 * IPC (INTER-PROCESS COMMUNICATION)
 * ============================================================================ */

int visor_ipc_send(pid_t dest, const void *msg, size_t len)
{
    return syscall3(SYS_IPC_SEND, dest, (long)msg, len);
}

ssize_t visor_ipc_recv(void *msg, size_t len, pid_t *sender)
{
    return syscall3(SYS_IPC_RECV, (long)msg, len, (long)sender);
}

int visor_shm_create(size_t size)
{
    return syscall1(SYS_SHM_CREATE, size);
}

void *visor_shm_attach(int shm_id)
{
    return (void *)syscall1(SYS_SHM_ATTACH, shm_id);
}

/* ============================================================================
 * THREADING (USERSPACE THREADS)
 * ============================================================================ */

static void thread_wrapper(void)
{
    /* Find current thread */
    thread_t *thread = NULL;
    for (uint32_t i = 0; i < MAX_THREADS; i++) {
        if (threads[i].active && threads[i].tid == next_tid - 1) {
            thread = &threads[i];
            break;
        }
    }
    
    if (thread) {
        void *result = thread->start_routine(thread->arg);
        thread->exit_code = (int)(long)result;
        thread->active = false;
    }
    
    visor_exit(0);
}

int visor_thread_create(uint32_t *tid, void *(*start_routine)(void *), void *arg)
{
    /* Find free thread slot */
    thread_t *thread = NULL;
    for (uint32_t i = 0; i < MAX_THREADS; i++) {
        if (!threads[i].active) {
            thread = &threads[i];
            break;
        }
    }
    
    if (!thread) return -1;
    
    /* Allocate stack */
    thread->stack = visor_malloc(THREAD_STACK_SIZE);
    if (!thread->stack) return -1;
    
    thread->tid = next_tid++;
    thread->start_routine = start_routine;
    thread->arg = arg;
    thread->active = true;
    
    *tid = thread->tid;
    
    /* TODO: Create actual thread using clone syscall */
    /* For now, this is a placeholder */
    
    return 0;
}

int visor_thread_join(uint32_t tid, void **retval)
{
    thread_t *thread = NULL;
    for (uint32_t i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == tid) {
            thread = &threads[i];
            break;
        }
    }
    
    if (!thread) return -1;
    
    /* Wait for thread to finish */
    while (thread->active) {
        visor_sleep(1);
    }
    
    if (retval) {
        *retval = (void *)(long)thread->exit_code;
    }
    
    visor_free(thread->stack);
    
    return 0;
}

/* ============================================================================
 * SYNCHRONIZATION PRIMITIVES
 * ============================================================================ */

void visor_mutex_init(visor_mutex_t *mutex)
{
    mutex->lock = 0;
    mutex->owner = 0;
}

void visor_mutex_lock(visor_mutex_t *mutex)
{
    uint32_t tmp;
    __asm__ volatile(
        "1: ldaxr %w0, [%1]; cbnz %w0, 1b; mov %w0, #1; stxr %w0, %w0, [%1]; cbnz %w0, 1b"
        : "=&r"(tmp) : "r"(&mutex->lock) : "memory"
    );
    mutex->owner = visor_getpid();
}

void visor_mutex_unlock(visor_mutex_t *mutex)
{
    mutex->owner = 0;
    __asm__ volatile("stlr wzr, [%0]" : : "r"(&mutex->lock) : "memory");
}

void visor_sem_init(visor_sem_t *sem, int32_t value)
{
    sem->count = value;
    sem->lock = 0;
}

void visor_sem_wait(visor_sem_t *sem)
{
    while (1) {
        /* Acquire lock */
        uint32_t tmp;
        __asm__ volatile(
            "1: ldaxr %w0, [%1]; cbnz %w0, 1b; mov %w0, #1; stxr %w0, %w0, [%1]; cbnz %w0, 1b"
            : "=&r"(tmp) : "r"(&sem->lock) : "memory"
        );
        
        if (sem->count > 0) {
            sem->count--;
            __asm__ volatile("stlr wzr, [%0]" : : "r"(&sem->lock) : "memory");
            break;
        }
        
        /* Release lock and retry */
        __asm__ volatile("stlr wzr, [%0]" : : "r"(&sem->lock) : "memory");
        visor_sleep(1);
    }
}

void visor_sem_post(visor_sem_t *sem)
{
    /* Acquire lock */
    uint32_t tmp;
    __asm__ volatile(
        "1: ldaxr %w0, [%1]; cbnz %w0, 1b; mov %w0, #1; stxr %w0, %w0, [%1]; cbnz %w0, 1b"
        : "=&r"(tmp) : "r"(&sem->lock) : "memory"
    );
    
    sem->count++;
    
    /* Release lock */
    __asm__ volatile("stlr wzr, [%0]" : : "r"(&sem->lock) : "memory");
}

/* ============================================================================
 * SERVICE APIs (HIGH-LEVEL)
 * ============================================================================ */

int visor_audio_play(const char *filename)
{
    return syscall1(SYS_AUDIO_PLAY, (long)filename);
}

int visor_camera_capture(void *buffer, size_t size)
{
    return syscall2(SYS_CAMERA_CAPTURE, (long)buffer, size);
}

int visor_bt_connect(const char *address)
{
    return syscall1(SYS_BT_CONNECT, (long)address);
}

int visor_haptic_vibrate(uint32_t duration_ms, uint16_t intensity)
{
    return syscall2(SYS_HAPTIC_VIBRATE, duration_ms, intensity);
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

void visor_print(const char *str)
{
    size_t len = 0;
    while (str[len]) len++;
    visor_write(STDOUT, str, len);
}

void visor_println(const char *str)
{
    visor_print(str);
    visor_write(STDOUT, "\n", 1);
}