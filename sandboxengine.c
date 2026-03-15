/*
 * sandbox_engine.c - Sandbox Engine
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Motor de sandboxing para aislamiento y seguridad de procesos.
 * Implementa múltiples capas de aislamiento inspiradas en Linux containers/seccomp.
 * 
 * CAPAS DE AISLAMIENTO:
 * 1. Resource Limits (CPU, memory, I/O)
 * 2. Capabilities (fine-grained permissions)
 * 3. Syscall Filtering (allowlist/denylist)
 * 4. Namespace Isolation (process, mount, network)
 * 5. Monitoring & Enforcement
 * 
 * INSPIRATION:
 * - Linux seccomp-BPF
 * - Linux capabilities
 * - Linux cgroups
 * - Linux namespaces
 * - Chrome/Docker sandboxing
 * 
 * PRODUCCIÓN - BARE-METAL - ZERO DEPENDENCIES
 */

#include "types.h"

extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *kalloc(size_t);
extern void kfree(void *);
extern uint64_t get_timer_count(void);

/* Capabilities (fine-grained permissions) */
#define CAP_NONE                0x00000000
#define CAP_CHOWN               0x00000001  /* Change file ownership */
#define CAP_DAC_OVERRIDE        0x00000002  /* Bypass file permission checks */
#define CAP_FOWNER              0x00000004  /* Bypass permission checks on operations */
#define CAP_FSETID              0x00000008  /* Don't clear SUID/SGID bits */
#define CAP_KILL                0x00000010  /* Send signals */
#define CAP_SETUID              0x00000020  /* Make arbitrary UID changes */
#define CAP_SETGID              0x00000040  /* Make arbitrary GID changes */
#define CAP_NET_BIND_SERVICE    0x00000080  /* Bind ports < 1024 */
#define CAP_NET_RAW             0x00000100  /* Use RAW/PACKET sockets */
#define CAP_SYS_ADMIN           0x00000200  /* System administration */
#define CAP_SYS_BOOT            0x00000400  /* Reboot system */
#define CAP_SYS_MODULE          0x00000800  /* Load/unload kernel modules */
#define CAP_SYS_PTRACE          0x00001000  /* Trace processes */
#define CAP_SYS_RESOURCE        0x00002000  /* Override resource limits */
#define CAP_SYS_TIME            0x00004000  /* Set system time */
#define CAP_ALL                 0xFFFFFFFF  /* All capabilities */

/* Syscall filter action */
typedef enum {
    SYSCALL_ALLOW   = 0,
    SYSCALL_DENY    = 1,
    SYSCALL_KILL    = 2,  /* Kill process on violation */
} syscall_action_t;

/* Syscall numbers (ARM64) */
#define SYSCALL_READ            63
#define SYSCALL_WRITE           64
#define SYSCALL_OPEN            56
#define SYSCALL_CLOSE           57
#define SYSCALL_MMAP            222
#define SYSCALL_MUNMAP          215
#define SYSCALL_BRK             214
#define SYSCALL_EXIT            93
#define SYSCALL_KILL            129
#define SYSCALL_FORK            220
#define SYSCALL_EXEC            221
#define SYSCALL_PTRACE          117
#define SYSCALL_REBOOT          142
#define SYSCALL_MODULE_LOAD     275

/* Resource types */
typedef enum {
    RESOURCE_CPU_TIME       = 0,  /* CPU time in ms */
    RESOURCE_MEMORY         = 1,  /* Memory in bytes */
    RESOURCE_IO_OPS         = 2,  /* I/O operations */
    RESOURCE_OPEN_FILES     = 3,  /* Open file count */
    RESOURCE_PROCESSES      = 4,  /* Process count */
} resource_type_t;

/* Sandbox profile */
typedef struct {
    char name[64];
    
    /* Capabilities */
    uint32_t capabilities;
    
    /* Syscall filter */
    syscall_action_t default_action;
    syscall_action_t syscall_filter[512];  /* Per-syscall action */
    
    /* Resource limits */
    uint64_t cpu_time_limit_ms;
    uint64_t memory_limit_bytes;
    uint32_t io_ops_limit;
    uint32_t open_files_limit;
    uint32_t processes_limit;
    
    /* Flags */
    bool read_only_root;
    bool network_isolated;
    bool no_new_privs;
    
} sandbox_profile_t;

/* Sandbox instance (per process) */
typedef struct {
    uint32_t pid;
    sandbox_profile_t *profile;
    
    /* Resource usage */
    uint64_t cpu_time_used_ms;
    uint64_t memory_used_bytes;
    uint32_t io_ops_used;
    uint32_t open_files_count;
    uint32_t child_processes;
    
    /* Violation tracking */
    uint32_t violations;
    uint32_t syscall_violations;
    uint32_t resource_violations;
    uint32_t capability_violations;
    
    /* State */
    bool active;
    uint64_t start_time;
    
} sandbox_instance_t;

/* Sandbox manager */
typedef struct {
    /* Profiles */
    sandbox_profile_t profiles[32];
    uint32_t num_profiles;
    
    /* Active sandboxes */
    sandbox_instance_t instances[256];
    uint32_t num_instances;
    
    /* Statistics */
    uint64_t total_violations;
    uint64_t processes_killed;
    
    volatile uint32_t lock;
    
} sandbox_manager_t;

/* Global state */
static sandbox_manager_t g_sandbox;

/* Predefined profiles */
static sandbox_profile_t g_profile_unrestricted;
static sandbox_profile_t g_profile_restricted;
static sandbox_profile_t g_profile_strict;
static sandbox_profile_t g_profile_compute_only;

/* Spinlock */
static inline void spinlock_acquire(volatile uint32_t *lock) {
    uint32_t tmp;
    __asm__ volatile(
        "1: ldaxr %w0, [%1]; cbnz %w0, 1b; mov %w0, #1; stxr %w0, %w0, [%1]; cbnz %w0, 1b"
        : "=&r"(tmp) : "r"(lock) : "memory"
    );
}

static inline void spinlock_release(volatile uint32_t *lock) {
    __asm__ volatile("stlr wzr, [%0]" : : "r"(lock) : "memory");
}

/* String utilities */
static size_t str_len(const char *s)
{
    size_t len = 0;
    while (*s++) len++;
    return len;
}

static char *str_cpy(char *dst, const char *src)
{
    char *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

/* Initialize predefined profiles */
static void init_profiles(void)
{
    /* UNRESTRICTED - No restrictions */
    memset(&g_profile_unrestricted, 0, sizeof(sandbox_profile_t));
    str_cpy(g_profile_unrestricted.name, "unrestricted");
    g_profile_unrestricted.capabilities = CAP_ALL;
    g_profile_unrestricted.default_action = SYSCALL_ALLOW;
    g_profile_unrestricted.cpu_time_limit_ms = 0;  /* No limit */
    g_profile_unrestricted.memory_limit_bytes = 0;
    g_profile_unrestricted.io_ops_limit = 0;
    g_profile_unrestricted.open_files_limit = 1024;
    g_profile_unrestricted.processes_limit = 100;
    
    /* RESTRICTED - Basic restrictions */
    memset(&g_profile_restricted, 0, sizeof(sandbox_profile_t));
    str_cpy(g_profile_restricted.name, "restricted");
    g_profile_restricted.capabilities = CAP_DAC_OVERRIDE | CAP_FOWNER | CAP_KILL;
    g_profile_restricted.default_action = SYSCALL_ALLOW;
    g_profile_restricted.cpu_time_limit_ms = 60000;  /* 60s */
    g_profile_restricted.memory_limit_bytes = 100 * 1024 * 1024;  /* 100MB */
    g_profile_restricted.io_ops_limit = 10000;
    g_profile_restricted.open_files_limit = 100;
    g_profile_restricted.processes_limit = 10;
    g_profile_restricted.no_new_privs = true;
    
    /* Deny dangerous syscalls */
    g_profile_restricted.syscall_filter[SYSCALL_PTRACE] = SYSCALL_DENY;
    g_profile_restricted.syscall_filter[SYSCALL_REBOOT] = SYSCALL_DENY;
    g_profile_restricted.syscall_filter[SYSCALL_MODULE_LOAD] = SYSCALL_DENY;
    
    /* STRICT - Highly restricted */
    memset(&g_profile_strict, 0, sizeof(sandbox_profile_t));
    str_cpy(g_profile_strict.name, "strict");
    g_profile_strict.capabilities = CAP_NONE;
    g_profile_strict.default_action = SYSCALL_DENY;
    g_profile_strict.cpu_time_limit_ms = 10000;  /* 10s */
    g_profile_strict.memory_limit_bytes = 10 * 1024 * 1024;  /* 10MB */
    g_profile_strict.io_ops_limit = 1000;
    g_profile_strict.open_files_limit = 10;
    g_profile_strict.processes_limit = 1;
    g_profile_strict.read_only_root = true;
    g_profile_strict.network_isolated = true;
    g_profile_strict.no_new_privs = true;
    
    /* Allow only essential syscalls */
    g_profile_strict.syscall_filter[SYSCALL_READ] = SYSCALL_ALLOW;
    g_profile_strict.syscall_filter[SYSCALL_WRITE] = SYSCALL_ALLOW;
    g_profile_strict.syscall_filter[SYSCALL_OPEN] = SYSCALL_ALLOW;
    g_profile_strict.syscall_filter[SYSCALL_CLOSE] = SYSCALL_ALLOW;
    g_profile_strict.syscall_filter[SYSCALL_MMAP] = SYSCALL_ALLOW;
    g_profile_strict.syscall_filter[SYSCALL_MUNMAP] = SYSCALL_ALLOW;
    g_profile_strict.syscall_filter[SYSCALL_BRK] = SYSCALL_ALLOW;
    g_profile_strict.syscall_filter[SYSCALL_EXIT] = SYSCALL_ALLOW;
    
    /* COMPUTE_ONLY - For pure computation */
    memset(&g_profile_compute_only, 0, sizeof(sandbox_profile_t));
    str_cpy(g_profile_compute_only.name, "compute_only");
    g_profile_compute_only.capabilities = CAP_NONE;
    g_profile_compute_only.default_action = SYSCALL_DENY;
    g_profile_compute_only.cpu_time_limit_ms = 30000;  /* 30s */
    g_profile_compute_only.memory_limit_bytes = 50 * 1024 * 1024;  /* 50MB */
    g_profile_compute_only.io_ops_limit = 0;  /* No I/O */
    g_profile_compute_only.open_files_limit = 0;
    g_profile_compute_only.processes_limit = 1;
    g_profile_compute_only.read_only_root = true;
    g_profile_compute_only.network_isolated = true;
    g_profile_compute_only.no_new_privs = true;
    
    /* Allow only memory + exit */
    g_profile_compute_only.syscall_filter[SYSCALL_MMAP] = SYSCALL_ALLOW;
    g_profile_compute_only.syscall_filter[SYSCALL_MUNMAP] = SYSCALL_ALLOW;
    g_profile_compute_only.syscall_filter[SYSCALL_BRK] = SYSCALL_ALLOW;
    g_profile_compute_only.syscall_filter[SYSCALL_EXIT] = SYSCALL_ALLOW;
}

/* Initialize sandbox engine */
void sandbox_init(void)
{
    uart_puts("[SANDBOX] Initializing sandbox engine\n");
    
    memset(&g_sandbox, 0, sizeof(sandbox_manager_t));
    
    /* Initialize predefined profiles */
    init_profiles();
    
    /* Register profiles */
    g_sandbox.profiles[0] = g_profile_unrestricted;
    g_sandbox.profiles[1] = g_profile_restricted;
    g_sandbox.profiles[2] = g_profile_strict;
    g_sandbox.profiles[3] = g_profile_compute_only;
    g_sandbox.num_profiles = 4;
    
    uart_puts("[SANDBOX] Loaded profiles: unrestricted, restricted, strict, compute_only\n");
    uart_puts("[SANDBOX] Sandbox engine initialized\n");
}

/* Create sandbox for process */
bool sandbox_create(uint32_t pid, const char *profile_name)
{
    spinlock_acquire(&g_sandbox.lock);
    
    /* Find profile */
    sandbox_profile_t *profile = NULL;
    for (uint32_t i = 0; i < g_sandbox.num_profiles; i++) {
        if (str_len(profile_name) == str_len(g_sandbox.profiles[i].name)) {
            /* Compare names */
            bool match = true;
            for (size_t j = 0; j < str_len(profile_name); j++) {
                if (profile_name[j] != g_sandbox.profiles[i].name[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                profile = &g_sandbox.profiles[i];
                break;
            }
        }
    }
    
    if (!profile) {
        spinlock_release(&g_sandbox.lock);
        return false;
    }
    
    /* Find free slot */
    sandbox_instance_t *instance = NULL;
    for (uint32_t i = 0; i < 256; i++) {
        if (!g_sandbox.instances[i].active) {
            instance = &g_sandbox.instances[i];
            break;
        }
    }
    
    if (!instance) {
        spinlock_release(&g_sandbox.lock);
        return false;
    }
    
    /* Initialize instance */
    memset(instance, 0, sizeof(sandbox_instance_t));
    instance->pid = pid;
    instance->profile = profile;
    instance->active = true;
    instance->start_time = get_timer_count();
    
    g_sandbox.num_instances++;
    
    spinlock_release(&g_sandbox.lock);
    
    uart_puts("[SANDBOX] Created sandbox for PID ");
    uart_put_dec(pid);
    uart_puts(" (profile: ");
    uart_puts(profile_name);
    uart_puts(")\n");
    
    return true;
}

/* Check syscall permission */
bool sandbox_check_syscall(uint32_t pid, uint32_t syscall_num)
{
    spinlock_acquire(&g_sandbox.lock);
    
    /* Find instance */
    sandbox_instance_t *inst = NULL;
    for (uint32_t i = 0; i < 256; i++) {
        if (g_sandbox.instances[i].active && g_sandbox.instances[i].pid == pid) {
            inst = &g_sandbox.instances[i];
            break;
        }
    }
    
    if (!inst) {
        spinlock_release(&g_sandbox.lock);
        return true;  /* No sandbox - allow */
    }
    
    /* Check syscall filter */
    syscall_action_t action = inst->profile->default_action;
    
    if (syscall_num < 512) {
        syscall_action_t specific = inst->profile->syscall_filter[syscall_num];
        if (specific != 0) {
            action = specific;
        }
    }
    
    if (action == SYSCALL_DENY || action == SYSCALL_KILL) {
        inst->violations++;
        inst->syscall_violations++;
        g_sandbox.total_violations++;
        
        uart_puts("[SANDBOX] Syscall violation: PID ");
        uart_put_dec(pid);
        uart_puts(" syscall ");
        uart_put_dec(syscall_num);
        uart_puts("\n");
        
        if (action == SYSCALL_KILL) {
            inst->active = false;
            g_sandbox.processes_killed++;
            
            uart_puts("[SANDBOX] KILLED process ");
            uart_put_dec(pid);
            uart_puts("\n");
        }
        
        spinlock_release(&g_sandbox.lock);
        return false;
    }
    
    spinlock_release(&g_sandbox.lock);
    return true;
}

/* Check capability */
bool sandbox_check_capability(uint32_t pid, uint32_t capability)
{
    spinlock_acquire(&g_sandbox.lock);
    
    sandbox_instance_t *inst = NULL;
    for (uint32_t i = 0; i < 256; i++) {
        if (g_sandbox.instances[i].active && g_sandbox.instances[i].pid == pid) {
            inst = &g_sandbox.instances[i];
            break;
        }
    }
    
    if (!inst) {
        spinlock_release(&g_sandbox.lock);
        return true;  /* No sandbox */
    }
    
    bool allowed = (inst->profile->capabilities & capability) != 0;
    
    if (!allowed) {
        inst->violations++;
        inst->capability_violations++;
        g_sandbox.total_violations++;
        
        uart_puts("[SANDBOX] Capability violation: PID ");
        uart_put_dec(pid);
        uart_puts(" cap 0x");
        uart_put_hex(capability);
        uart_puts("\n");
    }
    
    spinlock_release(&g_sandbox.lock);
    return allowed;
}

/* Update resource usage */
void sandbox_update_resource(uint32_t pid, resource_type_t type, uint64_t amount)
{
    spinlock_acquire(&g_sandbox.lock);
    
    sandbox_instance_t *inst = NULL;
    for (uint32_t i = 0; i < 256; i++) {
        if (g_sandbox.instances[i].active && g_sandbox.instances[i].pid == pid) {
            inst = &g_sandbox.instances[i];
            break;
        }
    }
    
    if (!inst) {
        spinlock_release(&g_sandbox.lock);
        return;
    }
    
    bool violation = false;
    
    switch (type) {
        case RESOURCE_CPU_TIME:
            inst->cpu_time_used_ms += amount;
            if (inst->profile->cpu_time_limit_ms > 0 &&
                inst->cpu_time_used_ms > inst->profile->cpu_time_limit_ms) {
                violation = true;
            }
            break;
            
        case RESOURCE_MEMORY:
            inst->memory_used_bytes = amount;
            if (inst->profile->memory_limit_bytes > 0 &&
                inst->memory_used_bytes > inst->profile->memory_limit_bytes) {
                violation = true;
            }
            break;
            
        case RESOURCE_IO_OPS:
            inst->io_ops_used += (uint32_t)amount;
            if (inst->profile->io_ops_limit > 0 &&
                inst->io_ops_used > inst->profile->io_ops_limit) {
                violation = true;
            }
            break;
            
        case RESOURCE_OPEN_FILES:
            inst->open_files_count = (uint32_t)amount;
            if (inst->open_files_count > inst->profile->open_files_limit) {
                violation = true;
            }
            break;
            
        case RESOURCE_PROCESSES:
            inst->child_processes = (uint32_t)amount;
            if (inst->child_processes > inst->profile->processes_limit) {
                violation = true;
            }
            break;
    }
    
    if (violation) {
        inst->violations++;
        inst->resource_violations++;
        g_sandbox.total_violations++;
        
        uart_puts("[SANDBOX] Resource violation: PID ");
        uart_put_dec(pid);
        uart_puts(" type ");
        uart_put_dec(type);
        uart_puts("\n");
        
        /* Kill on resource violation */
        inst->active = false;
        g_sandbox.processes_killed++;
        
        uart_puts("[SANDBOX] KILLED process ");
        uart_put_dec(pid);
        uart_puts(" (resource limit)\n");
    }
    
    spinlock_release(&g_sandbox.lock);
}

/* Destroy sandbox */
void sandbox_destroy(uint32_t pid)
{
    spinlock_acquire(&g_sandbox.lock);
    
    for (uint32_t i = 0; i < 256; i++) {
        if (g_sandbox.instances[i].active && g_sandbox.instances[i].pid == pid) {
            g_sandbox.instances[i].active = false;
            g_sandbox.num_instances--;
            break;
        }
    }
    
    spinlock_release(&g_sandbox.lock);
}

/* Print statistics */
void sandbox_print_stats(void)
{
    uart_puts("\n[SANDBOX] Statistics:\n");
    uart_puts("════════════════════════════════════════\n");
    
    uart_puts("  Active sandboxes:  ");
    uart_put_dec(g_sandbox.num_instances);
    uart_puts("\n");
    
    uart_puts("  Total violations:  ");
    uart_put_dec(g_sandbox.total_violations);
    uart_puts("\n");
    
    uart_puts("  Processes killed:  ");
    uart_put_dec(g_sandbox.processes_killed);
    uart_puts("\n");
    
    uart_puts("\n");
}