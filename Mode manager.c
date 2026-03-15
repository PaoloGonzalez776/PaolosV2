/*
 * mode_manager.c - Visor OS Mode Manager
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Gestiona los modos de operación del sistema:
 * - Smartphone: UI compacta, fullscreen
 * - Tablet: UI espaciosa, multitarea
 * - Laptop: Escritorio completo, productividad
 * - TV: UI kiosko, control remoto
 */

#include "types.h"

extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);
extern void *memset(void *s, int c, size_t n);
extern uint64_t get_timer_count(void);

/* Visor OS Modes */
typedef enum {
    VISOR_MODE_SMARTPHONE   = 0,
    VISOR_MODE_TABLET       = 1,
    VISOR_MODE_LAPTOP       = 2,
    VISOR_MODE_TV           = 3,
    VISOR_MODE_MAX          = 4
} visor_mode_t;

/* Window styles */
typedef enum {
    WINDOW_STYLE_FULLSCREEN     = 0,
    WINDOW_STYLE_ADAPTIVE       = 1,
    WINDOW_STYLE_FLOATING       = 2,
    WINDOW_STYLE_MAXIMIZED      = 3,
} window_style_t;

/* Task switcher styles */
typedef enum {
    TASK_SWITCHER_VERTICAL      = 0,
    TASK_SWITCHER_HORIZONTAL    = 1,
    TASK_SWITCHER_GRID          = 2,
    TASK_SWITCHER_CAROUSEL      = 3,
} task_switcher_t;

/* Navigation methods */
typedef enum {
    NAV_METHOD_TOUCH            = 0x01,
    NAV_METHOD_KEYBOARD         = 0x02,
    NAV_METHOD_MOUSE            = 0x04,
    NAV_METHOD_DPAD             = 0x08,
    NAV_METHOD_GESTURE          = 0x10,
    NAV_METHOD_VOICE            = 0x20,
} nav_method_t;

/* Display orientation */
typedef enum {
    ORIENTATION_PORTRAIT        = 0,
    ORIENTATION_LANDSCAPE       = 1,
    ORIENTATION_AUTO            = 2,
} orientation_t;

/* UI Configuration per mode */
typedef struct {
    window_style_t window_style;
    task_switcher_t task_switcher;
    
    bool show_taskbar;
    bool show_status_bar;
    bool virtual_keyboard;
    bool window_decorations;
    
    uint32_t nav_method_mask;
    orientation_t orientation;
    
    uint32_t ui_scale_percent;
    uint32_t animation_speed;
    
} ui_config_t;

/* Capability restrictions per mode */
typedef struct {
    uint32_t max_concurrent_apps;
    uint32_t max_visible_windows;
    
    bool allow_split_screen;
    bool allow_pip;
    bool allow_desktop_mode;
    bool allow_background_apps;
    bool allow_notifications;
    bool allow_widgets;
    
    uint32_t max_notification_priority;
    
} mode_capabilities_t;

/* Performance profile per mode */
typedef struct {
    uint32_t target_fps;
    uint32_t cpu_governor;
    uint32_t gpu_power_level;
    
    bool aggressive_memory_mgmt;
    bool suspend_background_apps;
    bool reduce_animations;
    
} performance_profile_t;

/* Mode transition */
typedef struct {
    visor_mode_t from_mode;
    visor_mode_t to_mode;
    uint64_t transition_time_ns;
    uint32_t flags;
} mode_transition_t;

/* Mode manager state */
typedef struct {
    visor_mode_t current_mode;
    visor_mode_t previous_mode;
    
    ui_config_t ui_config;
    mode_capabilities_t capabilities;
    performance_profile_t performance;
    
    uint64_t mode_start_time;
    uint64_t total_mode_time[VISOR_MODE_MAX];
    uint64_t mode_switch_count;
    
    mode_transition_t last_transition;
    
    bool locked;
    bool transition_in_progress;
    
    volatile uint32_t lock;
    
} mode_manager_t;

/* Mode change callback */
typedef void (*mode_change_callback_t)(visor_mode_t old_mode, visor_mode_t new_mode, void *user_data);

/* Callback registration */
typedef struct mode_callback {
    mode_change_callback_t callback;
    void *user_data;
    struct mode_callback *next;
} mode_callback_t;

/* Global state */
static mode_manager_t g_mode_manager;
static mode_callback_t *g_callbacks = NULL;
static volatile uint32_t g_callback_lock = 0;

/* Mode names */
static const char *g_mode_names[VISOR_MODE_MAX] = {
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

/* Configure UI for mode */
static void mode_configure_ui(visor_mode_t mode, ui_config_t *config)
{
    switch (mode) {
        case VISOR_MODE_SMARTPHONE:
            config->window_style = WINDOW_STYLE_FULLSCREEN;
            config->task_switcher = TASK_SWITCHER_VERTICAL;
            config->show_taskbar = false;
            config->show_status_bar = true;
            config->virtual_keyboard = true;
            config->window_decorations = false;
            config->nav_method_mask = NAV_METHOD_TOUCH | NAV_METHOD_GESTURE;
            config->orientation = ORIENTATION_AUTO;
            config->ui_scale_percent = 100;
            config->animation_speed = 300;
            break;
            
        case VISOR_MODE_TABLET:
            config->window_style = WINDOW_STYLE_ADAPTIVE;
            config->task_switcher = TASK_SWITCHER_GRID;
            config->show_taskbar = true;
            config->show_status_bar = true;
            config->virtual_keyboard = true;
            config->window_decorations = true;
            config->nav_method_mask = NAV_METHOD_TOUCH | NAV_METHOD_GESTURE;
            config->orientation = ORIENTATION_AUTO;
            config->ui_scale_percent = 110;
            config->animation_speed = 250;
            break;
            
        case VISOR_MODE_LAPTOP:
            config->window_style = WINDOW_STYLE_FLOATING;
            config->task_switcher = TASK_SWITCHER_HORIZONTAL;
            config->show_taskbar = true;
            config->show_status_bar = true;
            config->virtual_keyboard = false;
            config->window_decorations = true;
            config->nav_method_mask = NAV_METHOD_KEYBOARD | NAV_METHOD_MOUSE | NAV_METHOD_TOUCH;
            config->orientation = ORIENTATION_LANDSCAPE;
            config->ui_scale_percent = 100;
            config->animation_speed = 200;
            break;
            
        case VISOR_MODE_TV:
            config->window_style = WINDOW_STYLE_FULLSCREEN;
            config->task_switcher = TASK_SWITCHER_CAROUSEL;
            config->show_taskbar = false;
            config->show_status_bar = false;
            config->virtual_keyboard = false;
            config->window_decorations = false;
            config->nav_method_mask = NAV_METHOD_DPAD | NAV_METHOD_VOICE;
            config->orientation = ORIENTATION_LANDSCAPE;
            config->ui_scale_percent = 150;
            config->animation_speed = 400;
            break;
            
        default:
            break;
    }
}

/* Configure capabilities for mode */
static void mode_configure_capabilities(visor_mode_t mode, mode_capabilities_t *caps)
{
    switch (mode) {
        case VISOR_MODE_SMARTPHONE:
            caps->max_concurrent_apps = 1;
            caps->max_visible_windows = 1;
            caps->allow_split_screen = false;
            caps->allow_pip = true;
            caps->allow_desktop_mode = false;
            caps->allow_background_apps = true;
            caps->allow_notifications = true;
            caps->allow_widgets = true;
            caps->max_notification_priority = 3;
            break;
            
        case VISOR_MODE_TABLET:
            caps->max_concurrent_apps = 4;
            caps->max_visible_windows = 3;
            caps->allow_split_screen = true;
            caps->allow_pip = true;
            caps->allow_desktop_mode = false;
            caps->allow_background_apps = true;
            caps->allow_notifications = true;
            caps->allow_widgets = true;
            caps->max_notification_priority = 3;
            break;
            
        case VISOR_MODE_LAPTOP:
            caps->max_concurrent_apps = 32;
            caps->max_visible_windows = 20;
            caps->allow_split_screen = true;
            caps->allow_pip = true;
            caps->allow_desktop_mode = true;
            caps->allow_background_apps = true;
            caps->allow_notifications = true;
            caps->allow_widgets = true;
            caps->max_notification_priority = 3;
            break;
            
        case VISOR_MODE_TV:
            caps->max_concurrent_apps = 1;
            caps->max_visible_windows = 1;
            caps->allow_split_screen = false;
            caps->allow_pip = false;
            caps->allow_desktop_mode = false;
            caps->allow_background_apps = false;
            caps->allow_notifications = false;
            caps->allow_widgets = false;
            caps->max_notification_priority = 1;
            break;
            
        default:
            break;
    }
}

/* Configure performance for mode */
static void mode_configure_performance(visor_mode_t mode, performance_profile_t *perf)
{
    extern int cpufreq_set_governor(uint32_t, uint32_t);
    extern void pm_set_perf_state(uint32_t);
    
    switch (mode) {
        case VISOR_MODE_SMARTPHONE:
            perf->target_fps = 60;
            perf->cpu_governor = 4;
            perf->gpu_power_level = 1;
            perf->aggressive_memory_mgmt = true;
            perf->suspend_background_apps = true;
            perf->reduce_animations = false;
            
            for (uint32_t cpu = 0; cpu < 20; cpu++) {
                cpufreq_set_governor(cpu, 4);
            }
            pm_set_perf_state(2);
            break;
            
        case VISOR_MODE_TABLET:
            perf->target_fps = 90;
            perf->cpu_governor = 4;
            perf->gpu_power_level = 2;
            perf->aggressive_memory_mgmt = false;
            perf->suspend_background_apps = false;
            perf->reduce_animations = false;
            
            for (uint32_t cpu = 0; cpu < 20; cpu++) {
                cpufreq_set_governor(cpu, 4);
            }
            pm_set_perf_state(2);
            break;
            
        case VISOR_MODE_LAPTOP:
            perf->target_fps = 120;
            perf->cpu_governor = 5;
            perf->gpu_power_level = 3;
            perf->aggressive_memory_mgmt = false;
            perf->suspend_background_apps = false;
            perf->reduce_animations = false;
            
            for (uint32_t cpu = 0; cpu < 20; cpu++) {
                cpufreq_set_governor(cpu, 5);
            }
            pm_set_perf_state(1);
            break;
            
        case VISOR_MODE_TV:
            perf->target_fps = 60;
            perf->cpu_governor = 1;
            perf->gpu_power_level = 2;
            perf->aggressive_memory_mgmt = true;
            perf->suspend_background_apps = true;
            perf->reduce_animations = true;
            
            for (uint32_t cpu = 0; cpu < 20; cpu++) {
                cpufreq_set_governor(cpu, 1);
            }
            pm_set_perf_state(3);
            break;
            
        default:
            break;
    }
}

/* Notify registered callbacks */
static void mode_notify_callbacks(visor_mode_t old_mode, visor_mode_t new_mode)
{
    mode_callback_t *cb;
    
    spinlock_acquire(&g_callback_lock);
    
    cb = g_callbacks;
    while (cb) {
        if (cb->callback) {
            cb->callback(old_mode, new_mode, cb->user_data);
        }
        cb = cb->next;
    }
    
    spinlock_release(&g_callback_lock);
}

/* Initialize mode manager */
void mode_manager_init(void)
{
    uart_puts("[MODE_MANAGER] Initializing Visor OS mode manager\n");
    
    memset(&g_mode_manager, 0, sizeof(mode_manager_t));
    
    g_mode_manager.current_mode = VISOR_MODE_LAPTOP;
    g_mode_manager.previous_mode = VISOR_MODE_LAPTOP;
    
    mode_configure_ui(VISOR_MODE_LAPTOP, &g_mode_manager.ui_config);
    mode_configure_capabilities(VISOR_MODE_LAPTOP, &g_mode_manager.capabilities);
    mode_configure_performance(VISOR_MODE_LAPTOP, &g_mode_manager.performance);
    
    g_mode_manager.mode_start_time = get_timer_count();
    
    uart_puts("[MODE_MANAGER] Default mode: Laptop\n");
    uart_puts("[MODE_MANAGER] Mode manager initialized\n");
}

/* Set mode */
int mode_set(visor_mode_t mode)
{
    mode_manager_t *mgr = &g_mode_manager;
    visor_mode_t old_mode;
    uint64_t start_time, end_time;
    
    if (mode >= VISOR_MODE_MAX) {
        uart_puts("[MODE_MANAGER] ERROR: Invalid mode\n");
        return -1;
    }
    
    spinlock_acquire(&mgr->lock);
    
    if (mgr->locked) {
        uart_puts("[MODE_MANAGER] ERROR: Mode manager locked\n");
        spinlock_release(&mgr->lock);
        return -1;
    }
    
    if (mgr->transition_in_progress) {
        uart_puts("[MODE_MANAGER] ERROR: Transition already in progress\n");
        spinlock_release(&mgr->lock);
        return -1;
    }
    
    if (mode == mgr->current_mode) {
        spinlock_release(&mgr->lock);
        return 0;
    }
    
    mgr->transition_in_progress = true;
    old_mode = mgr->current_mode;
    
    spinlock_release(&mgr->lock);
    
    start_time = get_timer_count();
    
    uart_puts("[MODE_MANAGER] Switching mode: ");
    uart_puts(g_mode_names[old_mode]);
    uart_puts(" -> ");
    uart_puts(g_mode_names[mode]);
    uart_puts("\n");
    
    uint64_t mode_duration = start_time - mgr->mode_start_time;
    mgr->total_mode_time[old_mode] += mode_duration;
    
    mode_configure_ui(mode, &mgr->ui_config);
    mode_configure_capabilities(mode, &mgr->capabilities);
    mode_configure_performance(mode, &mgr->performance);
    
    extern void compositor_reconfigure(visor_mode_t);
    compositor_reconfigure(mode);
    
    mode_notify_callbacks(old_mode, mode);
    
    spinlock_acquire(&mgr->lock);
    
    mgr->previous_mode = old_mode;
    mgr->current_mode = mode;
    mgr->mode_start_time = get_timer_count();
    mgr->mode_switch_count++;
    
    end_time = get_timer_count();
    
    mgr->last_transition.from_mode = old_mode;
    mgr->last_transition.to_mode = mode;
    mgr->last_transition.transition_time_ns = end_time - start_time;
    
    mgr->transition_in_progress = false;
    
    spinlock_release(&mgr->lock);
    
    uart_puts("[MODE_MANAGER] Mode switched successfully (");
    uart_put_dec((end_time - start_time) / 1000000);
    uart_puts(" ms)\n");
    
    return 0;
}

/* Get current mode */
visor_mode_t mode_get_current(void)
{
    return g_mode_manager.current_mode;
}

/* Get previous mode */
visor_mode_t mode_get_previous(void)
{
    return g_mode_manager.previous_mode;
}

/* Get UI config */
const ui_config_t *mode_get_ui_config(void)
{
    return &g_mode_manager.ui_config;
}

/* Get capabilities */
const mode_capabilities_t *mode_get_capabilities(void)
{
    return &g_mode_manager.capabilities;
}

/* Get performance profile */
const performance_profile_t *mode_get_performance(void)
{
    return &g_mode_manager.performance;
}

/* Register mode change callback */
int mode_register_callback(mode_change_callback_t callback, void *user_data)
{
    mode_callback_t *cb;
    extern void *kalloc(size_t);
    
    if (!callback) return -1;
    
    cb = (mode_callback_t *)kalloc(sizeof(mode_callback_t));
    if (!cb) return -1;
    
    cb->callback = callback;
    cb->user_data = user_data;
    
    spinlock_acquire(&g_callback_lock);
    
    cb->next = g_callbacks;
    g_callbacks = cb;
    
    spinlock_release(&g_callback_lock);
    
    return 0;
}

/* Lock mode (prevent changes) */
void mode_lock(void)
{
    spinlock_acquire(&g_mode_manager.lock);
    g_mode_manager.locked = true;
    spinlock_release(&g_mode_manager.lock);
}

/* Unlock mode */
void mode_unlock(void)
{
    spinlock_acquire(&g_mode_manager.lock);
    g_mode_manager.locked = false;
    spinlock_release(&g_mode_manager.lock);
}

/* Get mode name */
const char *mode_get_name(visor_mode_t mode)
{
    if (mode >= VISOR_MODE_MAX) return "Unknown";
    return g_mode_names[mode];
}

/* Statistics */
void mode_print_stats(void)
{
    mode_manager_t *mgr = &g_mode_manager;
    
    uart_puts("\n[MODE_MANAGER] Statistics:\n");
    
    uart_puts("  Current mode:      ");
    uart_puts(g_mode_names[mgr->current_mode]);
    uart_puts("\n");
    
    uart_puts("  Mode switches:     ");
    uart_put_dec(mgr->mode_switch_count);
    uart_puts("\n");
    
    uart_puts("\n  Time per mode:\n");
    for (uint32_t i = 0; i < VISOR_MODE_MAX; i++) {
        if (mgr->total_mode_time[i] > 0) {
            uart_puts("    ");
            uart_puts(g_mode_names[i]);
            uart_puts(": ");
            uart_put_dec(mgr->total_mode_time[i] / 1000000000UL);
            uart_puts(" seconds\n");
        }
    }
    
    uart_puts("\n  Last transition:\n");
    uart_puts("    From: ");
    uart_puts(g_mode_names[mgr->last_transition.from_mode]);
    uart_puts("\n    To: ");
    uart_puts(g_mode_names[mgr->last_transition.to_mode]);
    uart_puts("\n    Time: ");
    uart_put_dec(mgr->last_transition.transition_time_ns / 1000000);
    uart_puts(" ms\n");
}