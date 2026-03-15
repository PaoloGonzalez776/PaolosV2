

#include "types.h"

extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *kalloc(size_t);
extern void kfree(void *);
extern uint64_t get_timer_count(void);

/* Boot stages */
typedef enum {
    BOOT_STAGE_FIRMWARE         = 0,   /* Firmware/bootloader */
    BOOT_STAGE_KERNEL_EARLY     = 1,   /* Early kernel init */
    BOOT_STAGE_KERNEL_CORE      = 2,   /* Core kernel subsystems */
    BOOT_STAGE_KERNEL_DRIVERS   = 3,   /* Hardware drivers */
    BOOT_STAGE_KERNEL_COMPLETE  = 4,   /* Kernel ready */
    BOOT_STAGE_USERSPACE_INIT   = 5,   /* Init process (PID 1) */
    BOOT_STAGE_SERVICES_CORE    = 6,   /* Core services */
    BOOT_STAGE_MODE_DETECT      = 7,   /* Mode detection */
    BOOT_STAGE_MODE_CONFIG      = 8,   /* Mode configuration */
    BOOT_STAGE_SERVICES_UI      = 9,   /* UI services */
    BOOT_STAGE_COMPLETE         = 10,  /* System ready */
    BOOT_STAGE_MAX              = 11,
} boot_stage_t;

/* Event types */
typedef enum {
    BOOT_EVENT_STAGE_START      = 0,
    BOOT_EVENT_STAGE_COMPLETE   = 1,
    BOOT_EVENT_SUBSYSTEM_INIT   = 2,
    BOOT_EVENT_SERVICE_START    = 3,
    BOOT_EVENT_SERVICE_FAIL     = 4,
    BOOT_EVENT_MODE_DETECT      = 5,
    BOOT_EVENT_MODE_SET         = 6,
    BOOT_EVENT_MILESTONE        = 7,
    BOOT_EVENT_ERROR            = 8,
    BOOT_EVENT_WARNING          = 9,
} boot_event_type_t;

/* Event severity */
typedef enum {
    BOOT_SEV_INFO               = 0,
    BOOT_SEV_WARNING            = 1,
    BOOT_SEV_ERROR              = 2,
    BOOT_SEV_CRITICAL           = 3,
} boot_severity_t;

/* Visor modes */
typedef enum {
    VISOR_MODE_SMARTPHONE       = 0,
    VISOR_MODE_TABLET           = 1,
    VISOR_MODE_LAPTOP           = 2,
    VISOR_MODE_TV               = 3,
} visor_mode_t;

/* Boot event */
typedef struct boot_event {
    uint32_t event_id;
    boot_event_type_t type;
    boot_severity_t severity;
    
    uint64_t timestamp_ns;
    uint32_t cpu_id;
    
    boot_stage_t stage;
    
    char message[128];
    
    uint32_t data0;
    uint32_t data1;
    
    struct boot_event *next;
    
} boot_event_t;

/* Stage timing */
typedef struct {
    boot_stage_t stage;
    const char *name;
    
    uint64_t start_time;
    uint64_t end_time;
    uint64_t duration;
    
    uint32_t event_count;
    uint32_t error_count;
    uint32_t warning_count;
    
    bool completed;
    
} stage_timing_t;

/* Boot logger state */
typedef struct {
    boot_stage_t current_stage;
    visor_mode_t detected_mode;
    
    uint64_t boot_start_time;
    uint64_t boot_complete_time;
    uint64_t total_boot_time;
    
    stage_timing_t stages[BOOT_STAGE_MAX];
    
    boot_event_t *events;
    boot_event_t *last_event;
    uint32_t num_events;
    uint32_t max_events;
    
    uint32_t total_errors;
    uint32_t total_warnings;
    
    bool boot_successful;
    bool early_panic;
    
    char boot_id[64];
    char hardware_id[64];
    
    volatile uint32_t lock;
    
} boot_logger_t;

/* Global state */
static boot_logger_t g_boot_logger;
static uint32_t g_next_event_id = 1;

/* Stage names */
static const char *g_stage_names[BOOT_STAGE_MAX] = {
    "Firmware/Bootloader",
    "Kernel Early Init",
    "Kernel Core Subsystems",
    "Kernel Drivers",
    "Kernel Complete",
    "User Space Init",
    "Core Services",
    "Mode Detection",
    "Mode Configuration",
    "UI Services",
    "Boot Complete",
};

/* Mode names */
static const char *g_mode_names[] = {
    "Smartphone",
    "Tablet",
    "Laptop",
    "TV"
};

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

/* Simple string copy */
static void strncpy_safe(char *dest, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

/* Get CPU ID */
static uint32_t boot_get_cpu_id(void)
{
    extern uint32_t cpu_get_id(void);
    return cpu_get_id();
}

/* Initialize boot logger */
void boot_logger_init(void)
{
    memset(&g_boot_logger, 0, sizeof(boot_logger_t));
    
    g_boot_logger.boot_start_time = get_timer_count();
    g_boot_logger.current_stage = BOOT_STAGE_KERNEL_EARLY;
    g_boot_logger.max_events = 512;
    g_boot_logger.detected_mode = VISOR_MODE_LAPTOP;  /* Default */
    
    /* Initialize stage timings */
    for (uint32_t i = 0; i < BOOT_STAGE_MAX; i++) {
        g_boot_logger.stages[i].stage = i;
        g_boot_logger.stages[i].name = g_stage_names[i];
    }
    
    /* Generate boot ID (timestamp-based) */
    uint64_t boot_ts = g_boot_logger.boot_start_time / 1000000;
    snprintf_simple(g_boot_logger.boot_id, sizeof(g_boot_logger.boot_id), 
                    "boot-%lu", (unsigned long)boot_ts);
    
    /* Hardware ID */
    strncpy_safe(g_boot_logger.hardware_id, "XR-Ultra-20C", sizeof(g_boot_logger.hardware_id));
    
    uart_puts("\n");
    uart_puts("╔════════════════════════════════════════════╗\n");
    uart_puts("║       VISOR OS BOOT LOGGER v1.0           ║\n");
    uart_puts("║   PaolosSilicon XR Ultra (20-core ARM64)  ║\n");
    uart_puts("╚════════════════════════════════════════════╝\n");
    uart_puts("\n");
    
    uart_puts("[BOOT] Boot ID: ");
    uart_puts(g_boot_logger.boot_id);
    uart_puts("\n");
    uart_puts("[BOOT] Hardware: ");
    uart_puts(g_boot_logger.hardware_id);
    uart_puts("\n");
    uart_puts("[BOOT] Logger initialized\n\n");
}

/* Simple snprintf */
static void snprintf_simple(char *buf, size_t size, const char *fmt, unsigned long val)
{
    const char *prefix = "boot-";
    size_t i = 0;
    
    /* Copy prefix */
    while (*prefix && i < size - 1) {
        buf[i++] = *prefix++;
    }
    
    /* Convert number to string */
    char tmp[32];
    int j = 0;
    if (val == 0) {
        tmp[j++] = '0';
    } else {
        while (val > 0 && j < 31) {
            tmp[j++] = '0' + (val % 10);
            val /= 10;
        }
    }
    
    /* Reverse and copy */
    while (j > 0 && i < size - 1) {
        buf[i++] = tmp[--j];
    }
    
    buf[i] = '\0';
}

/* Log boot event */
void boot_log_event(boot_event_type_t type, boot_severity_t severity, const char *message)
{
    boot_event_t *event;
    
    spinlock_acquire(&g_boot_logger.lock);
    
    if (g_boot_logger.num_events >= g_boot_logger.max_events) {
        spinlock_release(&g_boot_logger.lock);
        return;
    }
    
    event = (boot_event_t *)kalloc(sizeof(boot_event_t));
    if (!event) {
        spinlock_release(&g_boot_logger.lock);
        return;
    }
    
    memset(event, 0, sizeof(boot_event_t));
    
    event->event_id = g_next_event_id++;
    event->type = type;
    event->severity = severity;
    event->timestamp_ns = get_timer_count();
    event->cpu_id = boot_get_cpu_id();
    event->stage = g_boot_logger.current_stage;
    
    strncpy_safe(event->message, message, sizeof(event->message));
    
    /* Add to list */
    if (g_boot_logger.last_event) {
        g_boot_logger.last_event->next = event;
    } else {
        g_boot_logger.events = event;
    }
    g_boot_logger.last_event = event;
    g_boot_logger.num_events++;
    
    /* Update stage counters */
    stage_timing_t *stage = &g_boot_logger.stages[g_boot_logger.current_stage];
    stage->event_count++;
    
    if (severity == BOOT_SEV_ERROR || severity == BOOT_SEV_CRITICAL) {
        stage->error_count++;
        g_boot_logger.total_errors++;
    } else if (severity == BOOT_SEV_WARNING) {
        stage->warning_count++;
        g_boot_logger.total_warnings++;
    }
    
    spinlock_release(&g_boot_logger.lock);
    
    /* Print to console */
    uint64_t time_ms = (event->timestamp_ns - g_boot_logger.boot_start_time) / 1000000;
    
    uart_puts("[");
    uart_put_dec(time_ms);
    uart_puts("ms] ");
    
    switch (severity) {
        case BOOT_SEV_INFO:
            uart_puts("[INFO] ");
            break;
        case BOOT_SEV_WARNING:
            uart_puts("[WARN] ");
            break;
        case BOOT_SEV_ERROR:
            uart_puts("[ERROR] ");
            break;
        case BOOT_SEV_CRITICAL:
            uart_puts("[CRITICAL] ");
            break;
    }
    
    uart_puts(message);
    uart_puts("\n");
}

/* Start boot stage */
void boot_stage_start(boot_stage_t stage)
{
    stage_timing_t *st;
    
    spinlock_acquire(&g_boot_logger.lock);
    
    g_boot_logger.current_stage = stage;
    
    st = &g_boot_logger.stages[stage];
    st->start_time = get_timer_count();
    st->completed = false;
    
    spinlock_release(&g_boot_logger.lock);
    
    uart_puts("\n");
    uart_puts("════════════════════════════════════════════\n");
    uart_puts("  Stage: ");
    uart_puts(st->name);
    uart_puts("\n");
    uart_puts("════════════════════════════════════════════\n");
    
    boot_log_event(BOOT_EVENT_STAGE_START, BOOT_SEV_INFO, st->name);
}

/* Complete boot stage */
void boot_stage_complete(boot_stage_t stage)
{
    stage_timing_t *st;
    uint64_t duration_ms;
    char msg[128];
    
    spinlock_acquire(&g_boot_logger.lock);
    
    st = &g_boot_logger.stages[stage];
    st->end_time = get_timer_count();
    st->duration = st->end_time - st->start_time;
    st->completed = true;
    
    spinlock_release(&g_boot_logger.lock);
    
    duration_ms = st->duration / 1000000;
    
    uart_puts("──────────────────────────────────────────\n");
    uart_puts("  Stage Complete: ");
    uart_put_dec(duration_ms);
    uart_puts("ms");
    
    if (st->error_count > 0) {
        uart_puts(" (");
        uart_put_dec(st->error_count);
        uart_puts(" errors)");
    }
    
    uart_puts("\n\n");
    
    snprintf_simple(msg, sizeof(msg), "Stage complete", duration_ms);
    boot_log_event(BOOT_EVENT_STAGE_COMPLETE, BOOT_SEV_INFO, msg);
}

/* Log subsystem initialization */
void boot_log_subsystem(const char *name)
{
    char msg[128];
    
    uart_puts("  [INIT] ");
    uart_puts(name);
    uart_puts("\n");
    
    strncpy_safe(msg, "Initializing ", sizeof(msg) - 1);
    size_t len = 0;
    while (msg[len]) len++;
    strncpy_safe(msg + len, name, sizeof(msg) - len);
    
    boot_log_event(BOOT_EVENT_SUBSYSTEM_INIT, BOOT_SEV_INFO, msg);
}

/* Log service start */
void boot_log_service(const char *name, bool success)
{
    char msg[128];
    
    if (success) {
        uart_puts("  [SERVICE] Started: ");
        uart_puts(name);
        uart_puts("\n");
        
        strncpy_safe(msg, "Service started: ", sizeof(msg) - 1);
        size_t len = 0;
        while (msg[len]) len++;
        strncpy_safe(msg + len, name, sizeof(msg) - len);
        
        boot_log_event(BOOT_EVENT_SERVICE_START, BOOT_SEV_INFO, msg);
    } else {
        uart_puts("  [SERVICE] FAILED: ");
        uart_puts(name);
        uart_puts("\n");
        
        strncpy_safe(msg, "Service failed: ", sizeof(msg) - 1);
        size_t len = 0;
        while (msg[len]) len++;
        strncpy_safe(msg + len, name, sizeof(msg) - len);
        
        boot_log_event(BOOT_EVENT_SERVICE_FAIL, BOOT_SEV_ERROR, msg);
    }
}

/* Log mode detection */
void boot_log_mode_detect(visor_mode_t mode)
{
    char msg[128];
    
    spinlock_acquire(&g_boot_logger.lock);
    g_boot_logger.detected_mode = mode;
    spinlock_release(&g_boot_logger.lock);
    
    uart_puts("\n");
    uart_puts("╔════════════════════════════════════════════╗\n");
    uart_puts("║          MODE DETECTION COMPLETE          ║\n");
    uart_puts("╚════════════════════════════════════════════╝\n");
    uart_puts("  Detected Mode: ");
    uart_puts(g_mode_names[mode]);
    uart_puts("\n");
    uart_puts("  XR Hardware: SAME (20-core, dual 4K, IMU)\n");
    uart_puts("  UI Adaptation: ");
    
    switch (mode) {
        case VISOR_MODE_SMARTPHONE:
            uart_puts("Fullscreen, Touch, Compact");
            break;
        case VISOR_MODE_TABLET:
            uart_puts("Grid, Touch, Spacious");
            break;
        case VISOR_MODE_LAPTOP:
            uart_puts("Floating, Keyboard+Mouse");
            break;
        case VISOR_MODE_TV:
            uart_puts("Fullscreen, Remote, Kiosk");
            break;
    }
    uart_puts("\n\n");
    
    strncpy_safe(msg, "Mode detected: ", sizeof(msg) - 1);
    size_t len = 0;
    while (msg[len]) len++;
    strncpy_safe(msg + len, g_mode_names[mode], sizeof(msg) - len);
    
    boot_log_event(BOOT_EVENT_MODE_DETECT, BOOT_SEV_INFO, msg);
}

/* Log mode configuration */
void boot_log_mode_config(visor_mode_t mode)
{
    char msg[128];
    
    uart_puts("  [CONFIG] Configuring services for ");
    uart_puts(g_mode_names[mode]);
    uart_puts(" mode\n");
    uart_puts("  [CONFIG] Hardware unchanged, adapting UI/UX\n");
    
    strncpy_safe(msg, "Mode configured: ", sizeof(msg) - 1);
    size_t len = 0;
    while (msg[len]) len++;
    strncpy_safe(msg + len, g_mode_names[mode], sizeof(msg) - len);
    
    boot_log_event(BOOT_EVENT_MODE_SET, BOOT_SEV_INFO, msg);
}

/* Log milestone */
void boot_log_milestone(const char *milestone)
{
    uart_puts("\n★ MILESTONE: ");
    uart_puts(milestone);
    uart_puts("\n\n");
    
    boot_log_event(BOOT_EVENT_MILESTONE, BOOT_SEV_INFO, milestone);
}

/* Log error */
void boot_log_error(const char *error)
{
    uart_puts("\n✖ ERROR: ");
    uart_puts(error);
    uart_puts("\n\n");
    
    boot_log_event(BOOT_EVENT_ERROR, BOOT_SEV_ERROR, error);
}

/* Mark boot complete */
void boot_complete(bool successful)
{
    uint64_t total_ms;
    
    spinlock_acquire(&g_boot_logger.lock);
    
    g_boot_logger.boot_complete_time = get_timer_count();
    g_boot_logger.total_boot_time = g_boot_logger.boot_complete_time - g_boot_logger.boot_start_time;
    g_boot_logger.boot_successful = successful;
    
    spinlock_release(&g_boot_logger.lock);
    
    total_ms = g_boot_logger.total_boot_time / 1000000;
    
    uart_puts("\n");
    uart_puts("╔════════════════════════════════════════════╗\n");
    uart_puts("║          BOOT SEQUENCE COMPLETE           ║\n");
    uart_puts("╚════════════════════════════════════════════╝\n");
    uart_puts("\n");
    
    if (successful) {
        uart_puts("  Status:       ✓ SUCCESS\n");
    } else {
        uart_puts("  Status:       ✖ FAILED\n");
    }
    
    uart_puts("  Total Time:   ");
    uart_put_dec(total_ms);
    uart_puts(" ms\n");
    
    uart_puts("  Mode:         ");
    uart_puts(g_mode_names[g_boot_logger.detected_mode]);
    uart_puts("\n");
    
    uart_puts("  Events:       ");
    uart_put_dec(g_boot_logger.num_events);
    uart_puts("\n");
    
    if (g_boot_logger.total_errors > 0) {
        uart_puts("  Errors:       ");
        uart_put_dec(g_boot_logger.total_errors);
        uart_puts("\n");
    }
    
    if (g_boot_logger.total_warnings > 0) {
        uart_puts("  Warnings:     ");
        uart_put_dec(g_boot_logger.total_warnings);
        uart_puts("\n");
    }
    
    uart_puts("\n");
    
    boot_log_event(BOOT_EVENT_MILESTONE, BOOT_SEV_INFO, 
                   successful ? "Boot successful" : "Boot failed");
}

/* Print boot summary */
void boot_print_summary(void)
{
    uart_puts("\n");
    uart_puts("════════════════════════════════════════════\n");
    uart_puts("           BOOT TIMING BREAKDOWN            \n");
    uart_puts("════════════════════════════════════════════\n");
    
    for (uint32_t i = 0; i < BOOT_STAGE_MAX; i++) {
        stage_timing_t *st = &g_boot_logger.stages[i];
        
        if (!st->completed) continue;
        
        uint64_t duration_ms = st->duration / 1000000;
        
        uart_puts("\n");
        uart_puts(st->name);
        uart_puts(":\n");
        uart_puts("  Duration:  ");
        uart_put_dec(duration_ms);
        uart_puts(" ms\n");
        uart_puts("  Events:    ");
        uart_put_dec(st->event_count);
        uart_puts("\n");
        
        if (st->error_count > 0) {
            uart_puts("  Errors:    ");
            uart_put_dec(st->error_count);
            uart_puts("\n");
        }
        
        if (st->warning_count > 0) {
            uart_puts("  Warnings:  ");
            uart_put_dec(st->warning_count);
            uart_puts("\n");
        }
    }
    
    uart_puts("\n");
    uart_puts("════════════════════════════════════════════\n");
    uart_puts("  Total Boot Time: ");
    uart_put_dec(g_boot_logger.total_boot_time / 1000000);
    uart_puts(" ms\n");
    uart_puts("════════════════════════════════════════════\n");
    uart_puts("\n");
}

/* Get boot time */
uint64_t boot_get_time_ms(void)
{
    return g_boot_logger.total_boot_time / 1000000;
}

/* Get detected mode */
visor_mode_t boot_get_detected_mode(void)
{
    return g_boot_logger.detected_mode;
}

/* Check if boot was successful */
bool boot_was_successful(void)
{
    return g_boot_logger.boot_successful;
}