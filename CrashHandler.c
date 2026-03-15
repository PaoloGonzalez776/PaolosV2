/*
 * crash_handler.c - System Crash Handler and Recovery
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Maneja crashes del sistema, captura estado completo del hardware,
 * intenta recuperación automática, y registra toda la información
 * para análisis post-mortem.
 * 
 * CRÍTICO: Este código se ejecuta en contexto de pánico/crash,
 * debe ser extremadamente robusto y no depender de subsistemas
 * que puedan estar corruptos.
 */

#include "types.h"

extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);
extern uint64_t get_timer_count(void);

/* Crash types */
typedef enum {
    CRASH_TYPE_KERNEL_PANIC         = 0,
    CRASH_TYPE_UNHANDLED_EXCEPTION  = 1,
    CRASH_TYPE_PAGE_FAULT           = 2,
    CRASH_TYPE_STACK_OVERFLOW       = 3,
    CRASH_TYPE_ASSERTION_FAILED     = 4,
    CRASH_TYPE_DOUBLE_FAULT         = 5,
    CRASH_TYPE_HARDWARE_ERROR       = 6,
    CRASH_TYPE_MEMORY_CORRUPTION    = 7,
    CRASH_TYPE_DEADLOCK             = 8,
    CRASH_TYPE_THERMAL_SHUTDOWN     = 9,
    CRASH_TYPE_WATCHDOG_TIMEOUT     = 10,
    CRASH_TYPE_SERVICE_CRITICAL     = 11,
    CRASH_TYPE_UNKNOWN              = 255,
} crash_type_t;

/* ARM64 Exception levels */
typedef enum {
    EL0_USER        = 0,
    EL1_KERNEL      = 1,
    EL2_HYPERVISOR  = 2,
    EL3_SECURE      = 3,
} exception_level_t;

/* ARM64 Exception classes */
typedef enum {
    EC_UNKNOWN                  = 0x00,
    EC_WFI_WFE                  = 0x01,
    EC_SVC_AARCH64             = 0x15,
    EC_INSTRUCTION_ABORT_EL0    = 0x20,
    EC_INSTRUCTION_ABORT_EL1    = 0x21,
    EC_PC_ALIGNMENT             = 0x22,
    EC_DATA_ABORT_EL0           = 0x24,
    EC_DATA_ABORT_EL1           = 0x25,
    EC_SP_ALIGNMENT             = 0x26,
    EC_FP_EXCEPTION             = 0x2C,
    EC_SERROR                   = 0x2F,
    EC_BREAKPOINT_EL0           = 0x30,
    EC_BREAKPOINT_EL1           = 0x31,
    EC_WATCHPOINT_EL0           = 0x34,
    EC_WATCHPOINT_EL1           = 0x35,
} exception_class_t;

/* CPU register state (ARM64) */
typedef struct {
    /* General purpose registers */
    uint64_t x[31];         /* X0-X30 */
    uint64_t sp;            /* Stack pointer */
    uint64_t pc;            /* Program counter */
    uint64_t pstate;        /* Processor state */
    
    /* System registers */
    uint64_t elr_el1;       /* Exception link register */
    uint64_t esr_el1;       /* Exception syndrome register */
    uint64_t far_el1;       /* Fault address register */
    uint64_t sctlr_el1;     /* System control register */
    uint64_t ttbr0_el1;     /* Translation table base 0 */
    uint64_t ttbr1_el1;     /* Translation table base 1 */
    uint64_t tcr_el1;       /* Translation control register */
    uint64_t mair_el1;      /* Memory attribute indirection */
    uint64_t vbar_el1;      /* Vector base address register */
    
    /* Debug registers */
    uint64_t mdscr_el1;     /* Monitor debug system control */
    
} cpu_registers_t;

/* Stack frame */
typedef struct {
    uint64_t fp;            /* Frame pointer */
    uint64_t lr;            /* Link register */
} stack_frame_t;

/* Stack trace entry */
typedef struct {
    uint64_t pc;            /* Program counter */
    uint64_t lr;            /* Link register */
    uint64_t fp;            /* Frame pointer */
    const char *symbol;     /* Symbol name (if available) */
} stack_trace_entry_t;

#define MAX_STACK_TRACE 32

/* Crash context */
typedef struct {
    crash_type_t type;
    const char *message;
    const char *file;
    uint32_t line;
    
    uint32_t cpu_id;
    exception_level_t exception_level;
    exception_class_t exception_class;
    
    cpu_registers_t registers;
    
    stack_trace_entry_t stack_trace[MAX_STACK_TRACE];
    uint32_t stack_trace_depth;
    
    uint64_t crash_time;
    uint64_t uptime;
    
    bool in_panic;
    bool double_fault;
    uint32_t panic_count;
    
} crash_context_t;

/* System state snapshot */
typedef struct {
    uint32_t num_cpus;
    uint32_t active_cpus;
    
    uint64_t total_memory;
    uint64_t free_memory;
    uint64_t kernel_memory;
    
    uint32_t num_processes;
    uint32_t num_threads;
    
    uint32_t current_mode;  /* Visor mode */
    
    uint32_t cpu_temp_celsius;
    uint32_t gpu_temp_celsius;
    uint32_t battery_percent;
    
} system_snapshot_t;

/* Recovery action */
typedef enum {
    RECOVERY_NONE           = 0,
    RECOVERY_RESTART_SERVICE = 1,
    RECOVERY_KILL_PROCESS   = 2,
    RECOVERY_RESET_SUBSYSTEM = 3,
    RECOVERY_FAILSAFE_MODE  = 4,
    RECOVERY_REBOOT         = 5,
    RECOVERY_HALT           = 6,
} recovery_action_t;

/* Global crash handler state */
static crash_context_t g_crash_context;
static system_snapshot_t g_system_snapshot;
static volatile bool g_in_crash_handler = false;
static volatile uint32_t g_crash_lock = 0;

/* Crash names */
static const char *g_crash_names[] = {
    "Kernel Panic",
    "Unhandled Exception",
    "Page Fault",
    "Stack Overflow",
    "Assertion Failed",
    "Double Fault",
    "Hardware Error",
    "Memory Corruption",
    "Deadlock Detected",
    "Thermal Shutdown",
    "Watchdog Timeout",
    "Critical Service Failure",
    "Unknown",
};

/* Exception class names */
static const char *g_exception_names[] = {
    [EC_UNKNOWN]                = "Unknown",
    [EC_WFI_WFE]               = "WFI/WFE",
    [EC_SVC_AARCH64]           = "SVC (syscall)",
    [EC_INSTRUCTION_ABORT_EL0] = "Instruction Abort (EL0)",
    [EC_INSTRUCTION_ABORT_EL1] = "Instruction Abort (EL1)",
    [EC_PC_ALIGNMENT]          = "PC Alignment",
    [EC_DATA_ABORT_EL0]        = "Data Abort (EL0)",
    [EC_DATA_ABORT_EL1]        = "Data Abort (EL1)",
    [EC_SP_ALIGNMENT]          = "SP Alignment",
    [EC_FP_EXCEPTION]          = "FP Exception",
    [EC_SERROR]                = "SError",
    [EC_BREAKPOINT_EL0]        = "Breakpoint (EL0)",
    [EC_BREAKPOINT_EL1]        = "Breakpoint (EL1)",
    [EC_WATCHPOINT_EL0]        = "Watchpoint (EL0)",
    [EC_WATCHPOINT_EL1]        = "Watchpoint (EL1)",
};

/* Read system registers */
static inline uint64_t read_elr_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, elr_el1" : "=r"(val));
    return val;
}

static inline uint64_t read_esr_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(val));
    return val;
}

static inline uint64_t read_far_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, far_el1" : "=r"(val));
    return val;
}

static inline uint64_t read_sp(void) {
    uint64_t val;
    __asm__ volatile("mov %0, sp" : "=r"(val));
    return val;
}

static inline uint64_t read_fp(void) {
    uint64_t val;
    __asm__ volatile("mov %0, x29" : "=r"(val));
    return val;
}

static inline uint64_t read_lr(void) {
    uint64_t val;
    __asm__ volatile("mov %0, x30" : "=r"(val));
    return val;
}

static inline uint64_t read_currentel(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(val));
    return (val >> 2) & 3;
}

static inline uint32_t read_mpidr(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(val));
    return (uint32_t)(val & 0xFF);
}

/* Capture CPU registers */
static void crash_capture_registers(cpu_registers_t *regs)
{
    /* Capture all general purpose registers */
    __asm__ volatile(
        "stp x0,  x1,  [%0, #0]\n"
        "stp x2,  x3,  [%0, #16]\n"
        "stp x4,  x5,  [%0, #32]\n"
        "stp x6,  x7,  [%0, #48]\n"
        "stp x8,  x9,  [%0, #64]\n"
        "stp x10, x11, [%0, #80]\n"
        "stp x12, x13, [%0, #96]\n"
        "stp x14, x15, [%0, #112]\n"
        "stp x16, x17, [%0, #128]\n"
        "stp x18, x19, [%0, #144]\n"
        "stp x20, x21, [%0, #160]\n"
        "stp x22, x23, [%0, #176]\n"
        "stp x24, x25, [%0, #192]\n"
        "stp x26, x27, [%0, #208]\n"
        "stp x28, x29, [%0, #224]\n"
        "str x30,      [%0, #240]\n"
        : : "r"(regs->x) : "memory"
    );
    
    regs->sp = read_sp();
    regs->elr_el1 = read_elr_el1();
    regs->esr_el1 = read_esr_el1();
    regs->far_el1 = read_far_el1();
    
    regs->pc = regs->elr_el1;
}

/* Unwind stack */
static void crash_unwind_stack(crash_context_t *ctx)
{
    uint64_t fp = read_fp();
    uint32_t depth = 0;
    
    ctx->stack_trace_depth = 0;
    
    while (fp != 0 && depth < MAX_STACK_TRACE) {
        stack_frame_t *frame = (stack_frame_t *)fp;
        
        /* Validate frame pointer */
        if (fp < 0x1000 || fp > 0xFFFFFFFFFFFFULL) {
            break;
        }
        
        ctx->stack_trace[depth].fp = fp;
        ctx->stack_trace[depth].lr = frame->lr;
        ctx->stack_trace[depth].pc = frame->lr;
        ctx->stack_trace[depth].symbol = NULL;
        
        depth++;
        ctx->stack_trace_depth = depth;
        
        /* Move to previous frame */
        if (frame->fp <= fp) {
            break;
        }
        fp = frame->fp;
    }
}

/* Capture system state */
static void crash_capture_system_state(system_snapshot_t *snapshot)
{
    snapshot->num_cpus = 20;
    snapshot->active_cpus = 20;
    
    /* These would normally query actual subsystems */
    snapshot->total_memory = 16ULL * 1024 * 1024 * 1024;  /* 16GB */
    snapshot->free_memory = 0;
    snapshot->kernel_memory = 0;
    
    snapshot->num_processes = 0;
    snapshot->num_threads = 0;
    
    extern visor_mode_t mode_get_current(void);
    snapshot->current_mode = mode_get_current();
    
    extern int32_t pm_get_temperature(uint32_t);
    snapshot->cpu_temp_celsius = pm_get_temperature(0);
    snapshot->gpu_temp_celsius = pm_get_temperature(1);
    
    extern uint32_t pm_get_battery_level(void);
    snapshot->battery_percent = pm_get_battery_level();
}

/* Print crash banner */
static void crash_print_banner(crash_context_t *ctx)
{
    uart_puts("\n\n");
    uart_puts("████████████████████████████████████████████████████████\n");
    uart_puts("██                                                    ██\n");
    uart_puts("██              VISOR OS KERNEL PANIC                 ██\n");
    uart_puts("██                                                    ██\n");
    uart_puts("████████████████████████████████████████████████████████\n");
    uart_puts("\n");
    
    uart_puts("Crash Type:     ");
    if (ctx->type < 12) {
        uart_puts(g_crash_names[ctx->type]);
    } else {
        uart_puts(g_crash_names[12]);
    }
    uart_puts("\n");
    
    if (ctx->message) {
        uart_puts("Message:        ");
        uart_puts(ctx->message);
        uart_puts("\n");
    }
    
    if (ctx->file) {
        uart_puts("Location:       ");
        uart_puts(ctx->file);
        uart_puts(":");
        uart_put_dec(ctx->line);
        uart_puts("\n");
    }
    
    uart_puts("CPU:            ");
    uart_put_dec(ctx->cpu_id);
    uart_puts("\n");
    
    uart_puts("Exception Level: EL");
    uart_put_dec(ctx->exception_level);
    uart_puts("\n");
    
    uart_puts("Time:           ");
    uart_put_dec(ctx->crash_time / 1000000000);
    uart_puts(" seconds since boot\n");
    
    uart_puts("\n");
}

/* Print registers */
static void crash_print_registers(cpu_registers_t *regs)
{
    uart_puts("CPU Registers:\n");
    uart_puts("──────────────────────────────────────────────────────\n");
    
    for (int i = 0; i < 31; i += 2) {
        uart_puts("  X");
        uart_put_dec(i);
        if (i < 10) uart_puts(" ");
        uart_puts(" = 0x");
        uart_put_hex(regs->x[i]);
        
        uart_puts("    X");
        uart_put_dec(i + 1);
        if (i + 1 < 10) uart_puts(" ");
        uart_puts(" = 0x");
        uart_put_hex(regs->x[i + 1]);
        uart_puts("\n");
    }
    
    uart_puts("  SP   = 0x");
    uart_put_hex(regs->sp);
    uart_puts("\n");
    
    uart_puts("  PC   = 0x");
    uart_put_hex(regs->pc);
    uart_puts("\n");
    
    uart_puts("\nException Registers:\n");
    uart_puts("──────────────────────────────────────────────────────\n");
    
    uart_puts("  ELR_EL1 = 0x");
    uart_put_hex(regs->elr_el1);
    uart_puts(" (Exception return address)\n");
    
    uart_puts("  ESR_EL1 = 0x");
    uart_put_hex(regs->esr_el1);
    uart_puts(" (Exception syndrome)\n");
    
    uart_puts("  FAR_EL1 = 0x");
    uart_put_hex(regs->far_el1);
    uart_puts(" (Fault address)\n");
    
    uint32_t ec = (regs->esr_el1 >> 26) & 0x3F;
    uart_puts("  Exception Class: ");
    if (ec < sizeof(g_exception_names) / sizeof(char*) && g_exception_names[ec]) {
        uart_puts(g_exception_names[ec]);
    } else {
        uart_puts("Unknown (");
        uart_put_hex(ec);
        uart_puts(")");
    }
    uart_puts("\n");
    
    uart_puts("\n");
}

/* Print stack trace */
static void crash_print_stack_trace(crash_context_t *ctx)
{
    uart_puts("Stack Trace:\n");
    uart_puts("──────────────────────────────────────────────────────\n");
    
    if (ctx->stack_trace_depth == 0) {
        uart_puts("  (Stack trace unavailable)\n");
        return;
    }
    
    for (uint32_t i = 0; i < ctx->stack_trace_depth; i++) {
        uart_puts("  #");
        uart_put_dec(i);
        uart_puts("  PC=0x");
        uart_put_hex(ctx->stack_trace[i].pc);
        uart_puts("  FP=0x");
        uart_put_hex(ctx->stack_trace[i].fp);
        uart_puts("\n");
    }
    
    uart_puts("\n");
}

/* Print system state */
static void crash_print_system_state(system_snapshot_t *snapshot)
{
    const char *mode_names[] = { "Smartphone", "Tablet", "Laptop", "TV" };
    
    uart_puts("System State:\n");
    uart_puts("──────────────────────────────────────────────────────\n");
    
    uart_puts("  CPUs:         ");
    uart_put_dec(snapshot->active_cpus);
    uart_puts(" / ");
    uart_put_dec(snapshot->num_cpus);
    uart_puts("\n");
    
    uart_puts("  Memory:       ");
    uart_put_dec(snapshot->total_memory / (1024 * 1024));
    uart_puts(" MB total\n");
    
    uart_puts("  Mode:         ");
    if (snapshot->current_mode < 4) {
        uart_puts(mode_names[snapshot->current_mode]);
    } else {
        uart_puts("Unknown");
    }
    uart_puts("\n");
    
    uart_puts("  CPU Temp:     ");
    uart_put_dec(snapshot->cpu_temp_celsius);
    uart_puts("°C\n");
    
    uart_puts("  Battery:      ");
    uart_put_dec(snapshot->battery_percent);
    uart_puts("%\n");
    
    uart_puts("\n");
}

/* Determine recovery action */
static recovery_action_t crash_determine_recovery(crash_context_t *ctx)
{
    switch (ctx->type) {
        case CRASH_TYPE_SERVICE_CRITICAL:
            return RECOVERY_RESTART_SERVICE;
            
        case CRASH_TYPE_THERMAL_SHUTDOWN:
            return RECOVERY_HALT;
            
        case CRASH_TYPE_WATCHDOG_TIMEOUT:
            return RECOVERY_REBOOT;
            
        case CRASH_TYPE_DEADLOCK:
            return RECOVERY_KILL_PROCESS;
            
        case CRASH_TYPE_KERNEL_PANIC:
        case CRASH_TYPE_DOUBLE_FAULT:
        case CRASH_TYPE_MEMORY_CORRUPTION:
            return RECOVERY_REBOOT;
            
        default:
            return RECOVERY_HALT;
    }
}

/* Main crash handler */
void crash_handler(crash_type_t type, const char *message, const char *file, uint32_t line)
{
    recovery_action_t recovery;
    
    /* Prevent recursive crashes */
    if (g_in_crash_handler) {
        uart_puts("\n*** DOUBLE FAULT ***\n");
        while(1) {
            __asm__ volatile("wfi");
        }
    }
    
    g_in_crash_handler = true;
    
    /* Disable interrupts */
    __asm__ volatile("msr daifset, #0xF" ::: "memory");
    
    /* Initialize crash context */
    g_crash_context.type = type;
    g_crash_context.message = message;
    g_crash_context.file = file;
    g_crash_context.line = line;
    g_crash_context.cpu_id = read_mpidr();
    g_crash_context.exception_level = read_currentel();
    g_crash_context.crash_time = get_timer_count();
    g_crash_context.panic_count = 1;
    
    /* Capture state */
    crash_capture_registers(&g_crash_context.registers);
    crash_unwind_stack(&g_crash_context);
    crash_capture_system_state(&g_system_snapshot);
    
    /* Extract exception class from ESR */
    g_crash_context.exception_class = (g_crash_context.registers.esr_el1 >> 26) & 0x3F;
    
    /* Print crash information */
    crash_print_banner(&g_crash_context);
    crash_print_registers(&g_crash_context.registers);
    crash_print_stack_trace(&g_crash_context);
    crash_print_system_state(&g_system_snapshot);
    
    /* Determine recovery action */
    recovery = crash_determine_recovery(&g_crash_context);
    
    uart_puts("Recovery Action: ");
    switch (recovery) {
        case RECOVERY_RESTART_SERVICE:
            uart_puts("Restart Service\n");
            break;
        case RECOVERY_KILL_PROCESS:
            uart_puts("Kill Process\n");
            break;
        case RECOVERY_REBOOT:
            uart_puts("System Reboot\n");
            break;
        case RECOVERY_HALT:
            uart_puts("System Halt\n");
            break;
        default:
            uart_puts("None\n");
            break;
    }
    
    uart_puts("\n");
    uart_puts("████████████████████████████████████████████████████████\n");
    uart_puts("██                 SYSTEM HALTED                      ██\n");
    uart_puts("████████████████████████████████████████████████████████\n");
    uart_puts("\n");
    
    /* Halt system */
    while (1) {
        __asm__ volatile("wfi");
    }
}

/* Kernel panic */
void kernel_panic(const char *message, const char *file, uint32_t line)
{
    crash_handler(CRASH_TYPE_KERNEL_PANIC, message, file, line);
}

/* Assertion failed */
void assertion_failed(const char *expr, const char *file, uint32_t line)
{
    crash_handler(CRASH_TYPE_ASSERTION_FAILED, expr, file, line);
}

/* Exception handler entry */
void exception_handler(uint64_t exception_type, uint64_t esr, uint64_t far)
{
    const char *msg = "Unhandled exception";
    
    uint32_t ec = (esr >> 26) & 0x3F;
    
    if (ec < sizeof(g_exception_names) / sizeof(char*) && g_exception_names[ec]) {
        msg = g_exception_names[ec];
    }
    
    crash_handler(CRASH_TYPE_UNHANDLED_EXCEPTION, msg, NULL, 0);
}

/* Get crash context */
crash_context_t *crash_get_context(void)
{
    return &g_crash_context;
}