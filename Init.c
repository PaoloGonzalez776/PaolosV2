/*
 * init.c - User Space Init Process (PID 1)
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Primer proceso de user space. Responsable de:
 * - Inicializar servicios del sistema
 * - Montar sistemas de archivos
 * - Configurar modo de operación (Smartphone/Tablet/Laptop/TV)
 * - Lanzar compositor y shell
 * - Gestionar ciclo de vida de servicios
 */

#include "types.h"

extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);
extern void *memset(void *s, int c, size_t n);
extern uint64_t get_timer_count(void);

/* Service states */
typedef enum {
    SERVICE_STATE_STOPPED       = 0,
    SERVICE_STATE_STARTING      = 1,
    SERVICE_STATE_RUNNING       = 2,
    SERVICE_STATE_STOPPING      = 3,
    SERVICE_STATE_FAILED        = 4,
    SERVICE_STATE_RESTARTING    = 5,
} service_state_t;

/* Service types */
typedef enum {
    SERVICE_TYPE_CORE           = 0,  /* Critical system service */
    SERVICE_TYPE_DEVICE         = 1,  /* Device driver service */
    SERVICE_TYPE_DISPLAY        = 2,  /* Display/compositor */
    SERVICE_TYPE_INPUT          = 3,  /* Input management */
    SERVICE_TYPE_AUDIO          = 4,  /* Audio subsystem */
    SERVICE_TYPE_NETWORK        = 5,  /* Network stack */
    SERVICE_TYPE_APPLICATION    = 6,  /* User application */
} service_type_t;

/* Service priority */
typedef enum {
    SERVICE_PRIO_CRITICAL       = 0,
    SERVICE_PRIO_HIGH           = 1,
    SERVICE_PRIO_NORMAL         = 2,
    SERVICE_PRIO_LOW            = 3,
} service_priority_t;

/* Service flags */
#define SERVICE_FLAG_AUTOSTART      0x0001
#define SERVICE_FLAG_RESPAWN        0x0002
#define SERVICE_FLAG_ONESHOT        0x0004
#define SERVICE_FLAG_MODE_DEPENDENT 0x0008

/* Service function */
typedef int (*service_func_t)(void);

/* Service descriptor */
typedef struct service {
    const char *name;
    service_type_t type;
    service_priority_t priority;
    uint32_t flags;
    
    service_func_t init_func;
    service_func_t start_func;
    service_func_t stop_func;
    
    service_state_t state;
    
    uint32_t pid;
    uint32_t restart_count;
    uint64_t start_time;
    uint64_t total_runtime;
    
    uint32_t depends_on_count;
    struct service **depends_on;
    
    uint32_t required_mode_mask;
    
    struct service *next;
    
} service_t;

/* Boot stages */
typedef enum {
    BOOT_STAGE_EARLY_INIT       = 0,
    BOOT_STAGE_KERNEL_READY     = 1,
    BOOT_STAGE_DEVICES          = 2,
    BOOT_STAGE_FILESYSTEM       = 3,
    BOOT_STAGE_NETWORK          = 4,
    BOOT_STAGE_DISPLAY          = 5,
    BOOT_STAGE_MODE_DETECT      = 6,
    BOOT_STAGE_USER_SERVICES    = 7,
    BOOT_STAGE_COMPLETE         = 8,
} boot_stage_t;

/* Init state */
typedef struct {
    boot_stage_t current_stage;
    visor_mode_t detected_mode;
    
    service_t *services;
    uint32_t num_services;
    uint32_t num_running;
    
    bool emergency_mode;
    bool safe_mode;
    
    uint64_t boot_start_time;
    uint64_t boot_complete_time;
    
} init_state_t;

/* Global state */
static init_state_t g_init;

/* Mode detection (simplified) */
extern visor_mode_t mode_get_current(void);
extern void mode_manager_init(void);
extern int mode_set(visor_mode_t);

/* Kernel subsystems */
extern void kalloc_init(void);
extern void event_init(void);
extern void power_mgmt_init(void);
extern void cpufreq_init(void);
extern void cpu_idle_init(void);
extern void timer_init(void);
extern void framebuffer_init(void);
extern void input_init(void);
extern void gpu_init(void);

/* User space subsystems */
extern void compositor_init(void);
extern void mode_manager_init(void);

/* Forward declarations */
static int service_ipc_broker_init(void);
static int service_device_manager_init(void);
static int service_power_manager_init(void);
static int service_input_manager_init(void);
static int service_display_server_init(void);
static int service_compositor_init(void);
static int service_shell_init(void);
static int service_app_manager_init(void);

/* Service registry */
static service_t g_service_ipc_broker = {
    .name = "ipc-broker",
    .type = SERVICE_TYPE_CORE,
    .priority = SERVICE_PRIO_CRITICAL,
    .flags = SERVICE_FLAG_AUTOSTART | SERVICE_FLAG_RESPAWN,
    .init_func = service_ipc_broker_init,
    .required_mode_mask = 0x0F,  /* All modes */
};

static service_t g_service_device_manager = {
    .name = "device-manager",
    .type = SERVICE_TYPE_DEVICE,
    .priority = SERVICE_PRIO_CRITICAL,
    .flags = SERVICE_FLAG_AUTOSTART | SERVICE_FLAG_RESPAWN,
    .init_func = service_device_manager_init,
    .required_mode_mask = 0x0F,  /* All modes */
};

static service_t g_service_power_manager = {
    .name = "power-manager",
    .type = SERVICE_TYPE_CORE,
    .priority = SERVICE_PRIO_HIGH,
    .flags = SERVICE_FLAG_AUTOSTART | SERVICE_FLAG_RESPAWN,
    .init_func = service_power_manager_init,
    .required_mode_mask = 0x0F,  /* All modes */
};

static service_t g_service_input_manager = {
    .name = "input-manager",
    .type = SERVICE_TYPE_INPUT,
    .priority = SERVICE_PRIO_HIGH,
    .flags = SERVICE_FLAG_AUTOSTART | SERVICE_FLAG_RESPAWN,
    .init_func = service_input_manager_init,
    .required_mode_mask = 0x0F,  /* All modes */
};

static service_t g_service_display_server = {
    .name = "display-server",
    .type = SERVICE_TYPE_DISPLAY,
    .priority = SERVICE_PRIO_HIGH,
    .flags = SERVICE_FLAG_AUTOSTART | SERVICE_FLAG_RESPAWN,
    .init_func = service_display_server_init,
    .required_mode_mask = 0x0F,  /* All modes */
};

static service_t g_service_compositor = {
    .name = "compositor",
    .type = SERVICE_TYPE_DISPLAY,
    .priority = SERVICE_PRIO_HIGH,
    .flags = SERVICE_FLAG_AUTOSTART | SERVICE_FLAG_RESPAWN | SERVICE_FLAG_MODE_DEPENDENT,
    .init_func = service_compositor_init,
    .required_mode_mask = 0x0F,  /* All modes */
};

static service_t g_service_shell = {
    .name = "shell",
    .type = SERVICE_TYPE_DISPLAY,
    .priority = SERVICE_PRIO_NORMAL,
    .flags = SERVICE_FLAG_AUTOSTART | SERVICE_FLAG_RESPAWN | SERVICE_FLAG_MODE_DEPENDENT,
    .init_func = service_shell_init,
    .required_mode_mask = 0x0F,  /* All modes */
};

static service_t g_service_app_manager = {
    .name = "app-manager",
    .type = SERVICE_TYPE_CORE,
    .priority = SERVICE_PRIO_NORMAL,
    .flags = SERVICE_FLAG_AUTOSTART | SERVICE_FLAG_RESPAWN,
    .init_func = service_app_manager_init,
    .required_mode_mask = 0x0F,  /* All modes */
};

/* Service implementations */
static int service_ipc_broker_init(void)
{
    uart_puts("  [SERVICE] Starting IPC broker...\n");
    /* IPC broker manages inter-process communication */
    /* In real implementation, this would start IPC server */
    return 0;
}

static int service_device_manager_init(void)
{
    uart_puts("  [SERVICE] Starting device manager...\n");
    /* Device manager handles hotplug and device enumeration */
    return 0;
}

static int service_power_manager_init(void)
{
    uart_puts("  [SERVICE] Starting power manager...\n");
    power_mgmt_init();
    return 0;
}

static int service_input_manager_init(void)
{
    uart_puts("  [SERVICE] Starting input manager...\n");
    input_init();
    return 0;
}

static int service_display_server_init(void)
{
    uart_puts("  [SERVICE] Starting display server...\n");
    framebuffer_init();
    return 0;
}

static int service_compositor_init(void)
{
    uart_puts("  [SERVICE] Starting compositor...\n");
    compositor_init();
    return 0;
}

static int service_shell_init(void)
{
    uart_puts("  [SERVICE] Starting shell...\n");
    /* Shell provides UI chrome (taskbar, status bar, etc.) */
    return 0;
}

static int service_app_manager_init(void)
{
    uart_puts("  [SERVICE] Starting app manager...\n");
    /* App manager handles application lifecycle */
    return 0;
}

/* Register service */
static void init_register_service(service_t *service)
{
    if (!service) return;
    
    service->next = g_init.services;
    g_init.services = service;
    g_init.num_services++;
}

/* Start service */
static int init_start_service(service_t *service)
{
    uint64_t start_time;
    int ret;
    
    if (!service) return -1;
    
    if (service->state == SERVICE_STATE_RUNNING) {
        return 0;
    }
    
    uart_puts("[INIT] Starting service: ");
    uart_puts(service->name);
    uart_puts("\n");
    
    service->state = SERVICE_STATE_STARTING;
    start_time = get_timer_count();
    
    if (service->init_func) {
        ret = service->init_func();
        if (ret != 0) {
            uart_puts("[INIT] Service failed to start: ");
            uart_puts(service->name);
            uart_puts("\n");
            service->state = SERVICE_STATE_FAILED;
            return -1;
        }
    }
    
    service->state = SERVICE_STATE_RUNNING;
    service->start_time = start_time;
    g_init.num_running++;
    
    return 0;
}

/* Stop service */
static int init_stop_service(service_t *service)
{
    if (!service) return -1;
    
    if (service->state != SERVICE_STATE_RUNNING) {
        return 0;
    }
    
    uart_puts("[INIT] Stopping service: ");
    uart_puts(service->name);
    uart_puts("\n");
    
    service->state = SERVICE_STATE_STOPPING;
    
    if (service->stop_func) {
        service->stop_func();
    }
    
    service->state = SERVICE_STATE_STOPPED;
    service->total_runtime += (get_timer_count() - service->start_time);
    g_init.num_running--;
    
    return 0;
}

/* Detect operating mode */
static visor_mode_t init_detect_mode(void)
{
    uart_puts("[INIT] Detecting operating mode...\n");
    
    /* In real implementation, this would:
     * - Check device tree / ACPI
     * - Detect available hardware (touchscreen, keyboard, etc.)
     * - Check user preference
     * - Return appropriate mode
     */
    
    /* For now, default to Laptop mode */
    visor_mode_t mode = 2;  /* VISOR_MODE_LAPTOP */
    
    const char *mode_names[] = {
        "Smartphone",
        "Tablet", 
        "Laptop",
        "TV"
    };
    
    uart_puts("[INIT] Detected mode: ");
    uart_puts(mode_names[mode]);
    uart_puts("\n");
    
    return mode;
}

/* Initialize kernel subsystems */
static void init_kernel_subsystems(void)
{
    uart_puts("\n[INIT] Stage 1: Kernel Subsystems\n");
    uart_puts("=====================================\n");
    
    uart_puts("[INIT] Initializing memory allocator...\n");
    kalloc_init();
    
    uart_puts("[INIT] Initializing event system...\n");
    event_init();
    
    uart_puts("[INIT] Initializing timer...\n");
    timer_init();
    
    uart_puts("[INIT] Initializing CPU frequency scaling...\n");
    cpufreq_init();
    
    uart_puts("[INIT] Initializing CPU idle states...\n");
    cpu_idle_init();
    
    uart_puts("[INIT] Initializing GPU...\n");
    gpu_init();
    
    g_init.current_stage = BOOT_STAGE_KERNEL_READY;
}

/* Initialize devices */
static void init_devices(void)
{
    uart_puts("\n[INIT] Stage 2: Device Initialization\n");
    uart_puts("=====================================\n");
    
    init_start_service(&g_service_device_manager);
    init_start_service(&g_service_power_manager);
    
    g_init.current_stage = BOOT_STAGE_DEVICES;
}

/* Initialize display subsystem */
static void init_display(void)
{
    uart_puts("\n[INIT] Stage 3: Display Subsystem\n");
    uart_puts("=====================================\n");
    
    init_start_service(&g_service_display_server);
    init_start_service(&g_service_input_manager);
    
    g_init.current_stage = BOOT_STAGE_DISPLAY;
}

/* Detect and configure mode */
static void init_configure_mode(void)
{
    uart_puts("\n[INIT] Stage 4: Mode Detection\n");
    uart_puts("=====================================\n");
    
    mode_manager_init();
    
    g_init.detected_mode = init_detect_mode();
    
    if (g_init.detected_mode != 2) {  /* If not already laptop */
        mode_set(g_init.detected_mode);
    }
    
    g_init.current_stage = BOOT_STAGE_MODE_DETECT;
}

/* Start user services */
static void init_user_services(void)
{
    uart_puts("\n[INIT] Stage 5: User Services\n");
    uart_puts("=====================================\n");
    
    init_start_service(&g_service_ipc_broker);
    init_start_service(&g_service_compositor);
    init_start_service(&g_service_shell);
    init_start_service(&g_service_app_manager);
    
    g_init.current_stage = BOOT_STAGE_USER_SERVICES;
}

/* Print boot summary */
static void init_print_summary(void)
{
    uint64_t boot_time_ms;
    service_t *service;
    
    g_init.boot_complete_time = get_timer_count();
    boot_time_ms = (g_init.boot_complete_time - g_init.boot_start_time) / 1000000;
    
    uart_puts("\n");
    uart_puts("╔════════════════════════════════════════════╗\n");
    uart_puts("║         VISOR OS USER SPACE INIT          ║\n");
    uart_puts("╚════════════════════════════════════════════╝\n");
    uart_puts("\n");
    
    uart_puts("Boot Summary:\n");
    uart_puts("-------------\n");
    uart_puts("  Boot time:        ");
    uart_put_dec(boot_time_ms);
    uart_puts(" ms\n");
    
    uart_puts("  Operating mode:   ");
    const char *mode_names[] = {"Smartphone", "Tablet", "Laptop", "TV"};
    uart_puts(mode_names[g_init.detected_mode]);
    uart_puts("\n");
    
    uart_puts("  Services started: ");
    uart_put_dec(g_init.num_running);
    uart_puts(" / ");
    uart_put_dec(g_init.num_services);
    uart_puts("\n");
    
    uart_puts("\nRunning Services:\n");
    uart_puts("-----------------\n");
    
    service = g_init.services;
    while (service) {
        if (service->state == SERVICE_STATE_RUNNING) {
            uart_puts("  ✓ ");
            uart_puts(service->name);
            uart_puts("\n");
        }
        service = service->next;
    }
    
    uart_puts("\n");
    uart_puts("System ready. User space operational.\n");
    uart_puts("\n");
}

/* Main init loop */
static void init_main_loop(void)
{
    service_t *service;
    
    uart_puts("[INIT] Entering main service supervision loop\n");
    
    while (1) {
        service = g_init.services;
        while (service) {
            if (service->state == SERVICE_STATE_FAILED && 
                (service->flags & SERVICE_FLAG_RESPAWN)) {
                
                uart_puts("[INIT] Respawning failed service: ");
                uart_puts(service->name);
                uart_puts("\n");
                
                service->restart_count++;
                init_start_service(service);
            }
            
            service = service->next;
        }
        
        /* Sleep for 1 second */
        extern void timer_mdelay(uint32_t);
        timer_mdelay(1000);
    }
}

/* Init entry point */
void init_main(void)
{
    uart_puts("\n");
    uart_puts("╔════════════════════════════════════════════╗\n");
    uart_puts("║          VISOR OS INIT (PID 1)            ║\n");
    uart_puts("║      User Space Initialization            ║\n");
    uart_puts("╚════════════════════════════════════════════╝\n");
    uart_puts("\n");
    
    memset(&g_init, 0, sizeof(init_state_t));
    g_init.boot_start_time = get_timer_count();
    g_init.current_stage = BOOT_STAGE_EARLY_INIT;
    
    /* Register all services */
    init_register_service(&g_service_ipc_broker);
    init_register_service(&g_service_device_manager);
    init_register_service(&g_service_power_manager);
    init_register_service(&g_service_input_manager);
    init_register_service(&g_service_display_server);
    init_register_service(&g_service_compositor);
    init_register_service(&g_service_shell);
    init_register_service(&g_service_app_manager);
    
    /* Boot sequence */
    init_kernel_subsystems();
    init_devices();
    init_display();
    init_configure_mode();
    init_user_services();
    
    g_init.current_stage = BOOT_STAGE_COMPLETE;
    
    init_print_summary();
    
    /* Enter main supervision loop */
    init_main_loop();
}

/* Service query */
service_t *init_get_service(const char *name)
{
    service_t *service = g_init.services;
    
    while (service) {
        if (strcmp(service->name, name) == 0) {
            return service;
        }
        service = service->next;
    }
    
    return NULL;
}

/* Simple string compare */
static int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

/* Statistics */
void init_print_stats(void)
{
    service_t *service;
    
    uart_puts("\n[INIT] Service Statistics:\n");
    uart_puts("==========================\n");
    
    service = g_init.services;
    while (service) {
        uart_puts("\n");
        uart_puts(service->name);
        uart_puts(":\n");
        uart_puts("  State:        ");
        
        switch (service->state) {
            case SERVICE_STATE_RUNNING:
                uart_puts("Running");
                break;
            case SERVICE_STATE_STOPPED:
                uart_puts("Stopped");
                break;
            case SERVICE_STATE_FAILED:
                uart_puts("Failed");
                break;
            default:
                uart_puts("Unknown");
                break;
        }
        
        uart_puts("\n  Restarts:     ");
        uart_put_dec(service->restart_count);
        uart_puts("\n");
        
        service = service->next;
    }
}