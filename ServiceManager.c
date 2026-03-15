/*
 * service_manager.c - Service Manager
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Gestiona servicios del sistema que se adaptan según el modo de operación.
 * El MISMO hardware XR puede funcionar en diferentes modos:
 * - Phone Mode: UI compacta, apps fullscreen
 * - Tablet Mode: UI espaciosa, multitarea
 * - Laptop Mode: Desktop completo, ventanas flotantes
 * - TV Mode: UI kiosko, control remoto
 * 
 * Los servicios se reconfiguran dinámicamente sin reiniciar.
 */

#include "types.h"

extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);
extern void *memset(void *s, int c, size_t n);
extern void *kalloc(size_t);
extern void kfree(void *);
extern uint64_t get_timer_count(void);

/* Visor OS Modes (defined in mode_manager.c) */
typedef enum {
    VISOR_MODE_SMARTPHONE   = 0,
    VISOR_MODE_TABLET       = 1,
    VISOR_MODE_LAPTOP       = 2,
    VISOR_MODE_TV           = 3,
} visor_mode_t;

/* Service categories */
typedef enum {
    SERVICE_CAT_SYSTEM          = 0,  /* Always running (kernel-level) */
    SERVICE_CAT_DISPLAY         = 1,  /* Display/compositor services */
    SERVICE_CAT_INPUT           = 2,  /* Input handling services */
    SERVICE_CAT_XR              = 3,  /* XR-specific (IMU, tracking, etc.) */
    SERVICE_CAT_NETWORK         = 4,  /* Network stack */
    SERVICE_CAT_AUDIO           = 5,  /* Audio subsystem */
    SERVICE_CAT_STORAGE         = 6,  /* Storage/filesystem */
    SERVICE_CAT_APPLICATION     = 7,  /* User applications */
} service_category_t;

/* Service state */
typedef enum {
    SVC_STATE_UNINITIALIZED     = 0,
    SVC_STATE_INITIALIZED       = 1,
    SVC_STATE_STARTING          = 2,
    SVC_STATE_RUNNING           = 3,
    SVC_STATE_PAUSED            = 4,  /* Suspended due to mode */
    SVC_STATE_STOPPING          = 5,
    SVC_STATE_STOPPED           = 6,
    SVC_STATE_FAILED            = 7,
} service_state_t;

/* Service behavior per mode */
typedef struct {
    bool enabled;               /* Service runs in this mode */
    bool visible;               /* Service UI visible */
    bool background_allowed;    /* Can run in background */
    uint32_t priority_boost;    /* Priority adjustment */
} mode_behavior_t;

/* Service descriptor */
typedef struct service_descriptor {
    uint32_t service_id;
    const char *name;
    service_category_t category;
    
    /* Lifecycle callbacks */
    int (*init_func)(void);
    int (*start_func)(void);
    int (*stop_func)(void);
    int (*pause_func)(void);
    int (*resume_func)(void);
    int (*reconfigure_func)(visor_mode_t);  /* Called on mode change */
    
    /* State */
    service_state_t state;
    uint32_t pid;
    uint32_t base_priority;
    
    /* Mode-specific behavior */
    mode_behavior_t behavior[4];  /* One per mode */
    
    /* Runtime stats */
    uint64_t start_time;
    uint64_t total_runtime;
    uint32_t restart_count;
    uint32_t fail_count;
    
    /* Flags */
    bool auto_restart;
    bool critical;              /* System fails if this fails */
    bool mode_aware;            /* Reconfigures on mode change */
    
    struct service_descriptor *next;
    
} service_descriptor_t;

/* Service manager state */
typedef struct {
    service_descriptor_t *services;
    uint32_t num_services;
    uint32_t num_running;
    
    visor_mode_t current_mode;
    
    uint64_t total_mode_switches;
    uint64_t total_service_starts;
    uint64_t total_service_stops;
    uint64_t total_reconfigurations;
    
    volatile uint32_t lock;
    
} service_manager_t;

/* Global state */
static service_manager_t g_svc_mgr;

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

/* Service implementations - Forward declarations */
static int svc_display_server_init(void);
static int svc_display_server_start(void);
static int svc_display_server_reconfig(visor_mode_t mode);

static int svc_compositor_init(void);
static int svc_compositor_start(void);
static int svc_compositor_reconfig(visor_mode_t mode);

static int svc_input_manager_init(void);
static int svc_input_manager_start(void);
static int svc_input_manager_reconfig(visor_mode_t mode);

static int svc_xr_tracking_init(void);
static int svc_xr_tracking_start(void);
static int svc_xr_tracking_pause(void);
static int svc_xr_tracking_resume(void);
static int svc_xr_tracking_reconfig(visor_mode_t mode);

static int svc_power_manager_init(void);
static int svc_power_manager_start(void);
static int svc_power_manager_reconfig(visor_mode_t mode);

static int svc_network_stack_init(void);
static int svc_network_stack_start(void);

static int svc_audio_server_init(void);
static int svc_audio_server_start(void);

/* Service registry */
static service_descriptor_t g_services[] = {
    /* Display Server - Critical, always running */
    {
        .name = "display-server",
        .category = SERVICE_CAT_DISPLAY,
        .init_func = svc_display_server_init,
        .start_func = svc_display_server_start,
        .reconfigure_func = svc_display_server_reconfig,
        .base_priority = 200,
        .critical = true,
        .auto_restart = true,
        .mode_aware = true,
        .behavior = {
            /* Smartphone */ { .enabled = true, .visible = true, .background_allowed = false, .priority_boost = 0 },
            /* Tablet */     { .enabled = true, .visible = true, .background_allowed = false, .priority_boost = 0 },
            /* Laptop */     { .enabled = true, .visible = true, .background_allowed = false, .priority_boost = 0 },
            /* TV */         { .enabled = true, .visible = true, .background_allowed = false, .priority_boost = 0 },
        }
    },
    
    /* Compositor - Adapts to each mode */
    {
        .name = "compositor",
        .category = SERVICE_CAT_DISPLAY,
        .init_func = svc_compositor_init,
        .start_func = svc_compositor_start,
        .reconfigure_func = svc_compositor_reconfig,
        .base_priority = 190,
        .critical = true,
        .auto_restart = true,
        .mode_aware = true,
        .behavior = {
            /* Smartphone */ { .enabled = true, .visible = true, .background_allowed = false, .priority_boost = 10 },
            /* Tablet */     { .enabled = true, .visible = true, .background_allowed = false, .priority_boost = 10 },
            /* Laptop */     { .enabled = true, .visible = true, .background_allowed = false, .priority_boost = 5 },
            /* TV */         { .enabled = true, .visible = true, .background_allowed = false, .priority_boost = 0 },
        }
    },
    
    /* Input Manager - Different input methods per mode */
    {
        .name = "input-manager",
        .category = SERVICE_CAT_INPUT,
        .init_func = svc_input_manager_init,
        .start_func = svc_input_manager_start,
        .reconfigure_func = svc_input_manager_reconfig,
        .base_priority = 180,
        .critical = true,
        .auto_restart = true,
        .mode_aware = true,
        .behavior = {
            /* Smartphone */ { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 20 },
            /* Tablet */     { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 15 },
            /* Laptop */     { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 10 },
            /* TV */         { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 5 },
        }
    },
    
    /* XR Tracking - Only active in XR-heavy modes, paused in TV mode */
    {
        .name = "xr-tracking",
        .category = SERVICE_CAT_XR,
        .init_func = svc_xr_tracking_init,
        .start_func = svc_xr_tracking_start,
        .pause_func = svc_xr_tracking_pause,
        .resume_func = svc_xr_tracking_resume,
        .reconfigure_func = svc_xr_tracking_reconfig,
        .base_priority = 220,
        .critical = false,
        .auto_restart = true,
        .mode_aware = true,
        .behavior = {
            /* Smartphone */ { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 30 },
            /* Tablet */     { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 25 },
            /* Laptop */     { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 20 },
            /* TV */         { .enabled = false, .visible = false, .background_allowed = false, .priority_boost = 0 }, /* Paused in TV mode */
        }
    },
    
    /* Power Manager - Adjusts power profile per mode */
    {
        .name = "power-manager",
        .category = SERVICE_CAT_SYSTEM,
        .init_func = svc_power_manager_init,
        .start_func = svc_power_manager_start,
        .reconfigure_func = svc_power_manager_reconfig,
        .base_priority = 150,
        .critical = true,
        .auto_restart = true,
        .mode_aware = true,
        .behavior = {
            /* Smartphone */ { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 10 },
            /* Tablet */     { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 5 },
            /* Laptop */     { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 0 },
            /* TV */         { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 0 },
        }
    },
    
    /* Network Stack - Always running */
    {
        .name = "network-stack",
        .category = SERVICE_CAT_NETWORK,
        .init_func = svc_network_stack_init,
        .start_func = svc_network_stack_start,
        .base_priority = 140,
        .critical = false,
        .auto_restart = true,
        .mode_aware = false,
        .behavior = {
            /* Smartphone */ { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 0 },
            /* Tablet */     { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 0 },
            /* Laptop */     { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 0 },
            /* TV */         { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 0 },
        }
    },
    
    /* Audio Server - Always running */
    {
        .name = "audio-server",
        .category = SERVICE_CAT_AUDIO,
        .init_func = svc_audio_server_init,
        .start_func = svc_audio_server_start,
        .base_priority = 170,
        .critical = false,
        .auto_restart = true,
        .mode_aware = false,
        .behavior = {
            /* Smartphone */ { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 0 },
            /* Tablet */     { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 0 },
            /* Laptop */     { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 0 },
            /* TV */         { .enabled = true, .visible = false, .background_allowed = true, .priority_boost = 5 },
        }
    },
};

#define NUM_SERVICES (sizeof(g_services) / sizeof(service_descriptor_t))

/* Service implementations */

/* Display Server */
static int svc_display_server_init(void)
{
    uart_puts("  [SVC] Initializing display-server\n");
    extern int framebuffer_init(void);
    return framebuffer_init();
}

static int svc_display_server_start(void)
{
    uart_puts("  [SVC] Starting display-server\n");
    return 0;
}

static int svc_display_server_reconfig(visor_mode_t mode)
{
    uart_puts("  [SVC] Reconfiguring display-server for mode ");
    uart_put_dec(mode);
    uart_puts("\n");
    /* Display server already initialized, no changes needed */
    return 0;
}

/* Compositor */
static int svc_compositor_init(void)
{
    uart_puts("  [SVC] Initializing compositor\n");
    extern void compositor_init(void);
    compositor_init();
    return 0;
}

static int svc_compositor_start(void)
{
    uart_puts("  [SVC] Starting compositor\n");
    return 0;
}

static int svc_compositor_reconfig(visor_mode_t mode)
{
    uart_puts("  [SVC] Reconfiguring compositor for mode ");
    uart_put_dec(mode);
    uart_puts("\n");
    
    extern void compositor_reconfigure(visor_mode_t);
    compositor_reconfigure(mode);
    
    return 0;
}

/* Input Manager */
static int svc_input_manager_init(void)
{
    uart_puts("  [SVC] Initializing input-manager\n");
    extern void input_init(void);
    input_init();
    return 0;
}

static int svc_input_manager_start(void)
{
    uart_puts("  [SVC] Starting input-manager\n");
    return 0;
}

static int svc_input_manager_reconfig(visor_mode_t mode)
{
    uart_puts("  [SVC] Reconfiguring input-manager for mode ");
    uart_put_dec(mode);
    uart_puts("\n");
    
    /* All input hardware stays active, just change interpretation:
     * - Phone/Tablet: Focus on touchscreen + gestures
     * - Laptop: Focus on keyboard + mouse + touchscreen
     * - TV: Focus on D-pad/remote control
     */
    
    return 0;
}

/* XR Tracking */
static int svc_xr_tracking_init(void)
{
    uart_puts("  [SVC] Initializing xr-tracking (IMU, eye, hand)\n");
    /* XR tracking already initialized in input_init() */
    return 0;
}

static int svc_xr_tracking_start(void)
{
    uart_puts("  [SVC] Starting xr-tracking\n");
    return 0;
}

static int svc_xr_tracking_pause(void)
{
    uart_puts("  [SVC] Pausing xr-tracking\n");
    /* Pause IMU/eye/hand tracking polling to save power */
    return 0;
}

static int svc_xr_tracking_resume(void)
{
    uart_puts("  [SVC] Resuming xr-tracking\n");
    return 0;
}

static int svc_xr_tracking_reconfig(visor_mode_t mode)
{
    uart_puts("  [SVC] Reconfiguring xr-tracking for mode ");
    uart_put_dec(mode);
    uart_puts("\n");
    
    if (mode == VISOR_MODE_TV) {
        /* TV mode: pause XR tracking to save power */
        svc_xr_tracking_pause();
    } else {
        /* Other modes: resume XR tracking */
        svc_xr_tracking_resume();
    }
    
    return 0;
}

/* Power Manager */
static int svc_power_manager_init(void)
{
    uart_puts("  [SVC] Initializing power-manager\n");
    extern void power_mgmt_init(void);
    power_mgmt_init();
    return 0;
}

static int svc_power_manager_start(void)
{
    uart_puts("  [SVC] Starting power-manager\n");
    return 0;
}

static int svc_power_manager_reconfig(visor_mode_t mode)
{
    uart_puts("  [SVC] Reconfiguring power-manager for mode ");
    uart_put_dec(mode);
    uart_puts("\n");
    
    extern int pm_set_perf_state(uint32_t);
    
    switch (mode) {
        case VISOR_MODE_SMARTPHONE:
            pm_set_perf_state(2);  /* Balanced */
            break;
        case VISOR_MODE_TABLET:
            pm_set_perf_state(2);  /* Balanced */
            break;
        case VISOR_MODE_LAPTOP:
            pm_set_perf_state(1);  /* High performance */
            break;
        case VISOR_MODE_TV:
            pm_set_perf_state(3);  /* Power saver */
            break;
    }
    
    return 0;
}

/* Network Stack */
static int svc_network_stack_init(void)
{
    uart_puts("  [SVC] Initializing network-stack\n");
    /* Network stack initialization */
    return 0;
}

static int svc_network_stack_start(void)
{
    uart_puts("  [SVC] Starting network-stack\n");
    return 0;
}

/* Audio Server */
static int svc_audio_server_init(void)
{
    uart_puts("  [SVC] Initializing audio-server\n");
    /* Audio server initialization */
    return 0;
}

static int svc_audio_server_start(void)
{
    uart_puts("  [SVC] Starting audio-server\n");
    return 0;
}

/* Initialize service manager */
void service_manager_init(void)
{
    uart_puts("[SERVICE_MANAGER] Initializing service manager\n");
    
    memset(&g_svc_mgr, 0, sizeof(service_manager_t));
    
    g_svc_mgr.current_mode = VISOR_MODE_LAPTOP;
    
    for (uint32_t i = 0; i < NUM_SERVICES; i++) {
        g_services[i].service_id = i;
        g_services[i].state = SVC_STATE_UNINITIALIZED;
    }
    
    g_svc_mgr.services = g_services;
    g_svc_mgr.num_services = NUM_SERVICES;
    
    uart_puts("[SERVICE_MANAGER] Registered ");
    uart_put_dec(NUM_SERVICES);
    uart_puts(" services\n");
}

/* Start service */
int service_start(uint32_t service_id)
{
    service_descriptor_t *svc;
    int ret;
    
    if (service_id >= NUM_SERVICES) return -1;
    
    svc = &g_services[service_id];
    
    spinlock_acquire(&g_svc_mgr.lock);
    
    if (svc->state == SVC_STATE_RUNNING) {
        spinlock_release(&g_svc_mgr.lock);
        return 0;
    }
    
    uart_puts("[SERVICE_MANAGER] Starting: ");
    uart_puts(svc->name);
    uart_puts("\n");
    
    svc->state = SVC_STATE_STARTING;
    
    if (svc->state == SVC_STATE_UNINITIALIZED && svc->init_func) {
        ret = svc->init_func();
        if (ret != 0) {
            svc->state = SVC_STATE_FAILED;
            svc->fail_count++;
            spinlock_release(&g_svc_mgr.lock);
            return -1;
        }
        svc->state = SVC_STATE_INITIALIZED;
    }
    
    if (svc->start_func) {
        ret = svc->start_func();
        if (ret != 0) {
            svc->state = SVC_STATE_FAILED;
            svc->fail_count++;
            spinlock_release(&g_svc_mgr.lock);
            return -1;
        }
    }
    
    svc->state = SVC_STATE_RUNNING;
    svc->start_time = get_timer_count();
    g_svc_mgr.num_running++;
    g_svc_mgr.total_service_starts++;
    
    spinlock_release(&g_svc_mgr.lock);
    
    return 0;
}

/* Stop service */
int service_stop(uint32_t service_id)
{
    service_descriptor_t *svc;
    
    if (service_id >= NUM_SERVICES) return -1;
    
    svc = &g_services[service_id];
    
    spinlock_acquire(&g_svc_mgr.lock);
    
    if (svc->state != SVC_STATE_RUNNING && svc->state != SVC_STATE_PAUSED) {
        spinlock_release(&g_svc_mgr.lock);
        return 0;
    }
    
    uart_puts("[SERVICE_MANAGER] Stopping: ");
    uart_puts(svc->name);
    uart_puts("\n");
    
    svc->state = SVC_STATE_STOPPING;
    
    if (svc->stop_func) {
        svc->stop_func();
    }
    
    svc->state = SVC_STATE_STOPPED;
    svc->total_runtime += (get_timer_count() - svc->start_time);
    g_svc_mgr.num_running--;
    g_svc_mgr.total_service_stops++;
    
    spinlock_release(&g_svc_mgr.lock);
    
    return 0;
}

/* Start all services for current mode */
void service_manager_start_all(void)
{
    visor_mode_t mode = g_svc_mgr.current_mode;
    
    uart_puts("[SERVICE_MANAGER] Starting all services for mode ");
    uart_put_dec(mode);
    uart_puts("\n");
    
    for (uint32_t i = 0; i < NUM_SERVICES; i++) {
        service_descriptor_t *svc = &g_services[i];
        
        if (svc->behavior[mode].enabled) {
            service_start(i);
        }
    }
}

/* Reconfigure all services for new mode */
void service_manager_reconfigure_for_mode(visor_mode_t new_mode)
{
    visor_mode_t old_mode = g_svc_mgr.current_mode;
    
    uart_puts("[SERVICE_MANAGER] Reconfiguring services: mode ");
    uart_put_dec(old_mode);
    uart_puts(" -> ");
    uart_put_dec(new_mode);
    uart_puts("\n");
    
    spinlock_acquire(&g_svc_mgr.lock);
    
    g_svc_mgr.current_mode = new_mode;
    g_svc_mgr.total_mode_switches++;
    
    spinlock_release(&g_svc_mgr.lock);
    
    for (uint32_t i = 0; i < NUM_SERVICES; i++) {
        service_descriptor_t *svc = &g_services[i];
        
        bool old_enabled = svc->behavior[old_mode].enabled;
        bool new_enabled = svc->behavior[new_mode].enabled;
        
        if (old_enabled && !new_enabled) {
            /* Service should pause in new mode */
            if (svc->pause_func) {
                uart_puts("  [SVC] Pausing: ");
                uart_puts(svc->name);
                uart_puts("\n");
                svc->pause_func();
                svc->state = SVC_STATE_PAUSED;
            } else {
                service_stop(i);
            }
        } else if (!old_enabled && new_enabled) {
            /* Service should start in new mode */
            service_start(i);
        } else if (old_enabled && new_enabled) {
            /* Service runs in both modes, reconfigure if mode-aware */
            if (svc->mode_aware && svc->reconfigure_func) {
                svc->reconfigure_func(new_mode);
                g_svc_mgr.total_reconfigurations++;
            }
        }
    }
    
    uart_puts("[SERVICE_MANAGER] Mode reconfiguration complete\n");
}

/* Get service state */
service_state_t service_get_state(uint32_t service_id)
{
    if (service_id >= NUM_SERVICES) return SVC_STATE_UNINITIALIZED;
    return g_services[service_id].state;
}

/* Statistics */
void service_manager_print_stats(void)
{
    uart_puts("\n[SERVICE_MANAGER] Statistics:\n");
    uart_puts("==============================\n");
    
    uart_puts("  Current mode:      ");
    uart_put_dec(g_svc_mgr.current_mode);
    uart_puts("\n");
    
    uart_puts("  Total services:    ");
    uart_put_dec(g_svc_mgr.num_services);
    uart_puts("\n");
    
    uart_puts("  Running services:  ");
    uart_put_dec(g_svc_mgr.num_running);
    uart_puts("\n");
    
    uart_puts("  Mode switches:     ");
    uart_put_dec(g_svc_mgr.total_mode_switches);
    uart_puts("\n");
    
    uart_puts("  Reconfigurations:  ");
    uart_put_dec(g_svc_mgr.total_reconfigurations);
    uart_puts("\n");
    
    uart_puts("\nService States:\n");
    uart_puts("---------------\n");
    
    for (uint32_t i = 0; i < NUM_SERVICES; i++) {
        service_descriptor_t *svc = &g_services[i];
        
        uart_puts("  ");
        uart_puts(svc->name);
        uart_puts(": ");
        
        switch (svc->state) {
            case SVC_STATE_RUNNING:
                uart_puts("RUNNING");
                break;
            case SVC_STATE_PAUSED:
                uart_puts("PAUSED");
                break;
            case SVC_STATE_STOPPED:
                uart_puts("STOPPED");
                break;
            case SVC_STATE_FAILED:
                uart_puts("FAILED");
                break;
            default:
                uart_puts("UNKNOWN");
                break;
        }
        
        uart_puts("\n");
    }
}