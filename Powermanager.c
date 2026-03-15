/*
 * power_manager.c - Power Management Service
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Sistema de gestión de energía para XR headset.
 * Control de batería, térmica, estados de energía, DVFS.
 * 
 * CRÍTICO PARA XR HEADSET:
 * - Battery life: 2-4 horas de uso intensivo
 * - Thermal management: Prevenir overheating (cerca de la cara)
 * - Performance vs Power: Balance dinámico
 * - Safety: Protección de batería y temperatura
 * 
 * CARACTERÍSTICAS:
 * - Battery monitoring (voltage, current, SoC)
 * - Power states (Active, Idle, Sleep, Deep Sleep)
 * - DVFS (Dynamic Voltage and Frequency Scaling)
 * - Thermal management (throttling)
 * - Power budgeting (display, cameras, CPU, GPU)
 * - Wake locks (prevent sleep)
 * - Charging control
 * - Battery health tracking
 * 
 * POWER STATES:
 * - ACTIVE:      Full performance (displays on, tracking active)
 * - IDLE:        Reduced performance (no movement detected)
 * - SLEEP:       Low power (displays off, tracking paused)
 * - DEEP_SLEEP:  Ultra-low power (only wake sources active)
 * 
 * THERMAL ZONES:
 * - CPU/GPU: Processing units
 * - Display: OLED panels (hot)
 * - Battery: Li-ion safety
 * - Face area: User comfort (<40°C)
 * 
 * INSPIRATION:
 * - Android PowerManager
 * - Linux cpufreq/thermal
 * - iOS power management
 * - Meta Quest power system
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

/* Power constants */
#define MAX_THERMAL_ZONES       8       /* CPU, GPU, Display, Battery, etc */
#define MAX_WAKE_LOCKS          32      /* Max wake locks */
#define MAX_POWER_DOMAINS       16      /* Power domains */

/* Power states */
typedef enum {
    POWER_STATE_ACTIVE      = 0,  /* Full performance */
    POWER_STATE_IDLE        = 1,  /* Reduced performance */
    POWER_STATE_SLEEP       = 2,  /* Low power */
    POWER_STATE_DEEP_SLEEP  = 3,  /* Ultra-low power */
    POWER_STATE_SHUTDOWN    = 4,  /* Power off */
} power_state_t;

/* CPU frequency levels */
typedef enum {
    CPU_FREQ_ULTRA_LOW  = 0,  /*  600 MHz */
    CPU_FREQ_LOW        = 1,  /* 1200 MHz */
    CPU_FREQ_MEDIUM     = 2,  /* 1800 MHz */
    CPU_FREQ_HIGH       = 3,  /* 2400 MHz */
    CPU_FREQ_TURBO      = 4,  /* 3000 MHz */
} cpu_freq_level_t;

/* Thermal trip points */
typedef enum {
    THERMAL_NORMAL      = 0,  /* < 60°C */
    THERMAL_WARM        = 1,  /* 60-70°C */
    THERMAL_HOT         = 2,  /* 70-80°C */
    THERMAL_CRITICAL    = 3,  /* 80-90°C */
    THERMAL_EMERGENCY   = 4,  /* > 90°C */
} thermal_level_t;

/* Wake lock types */
typedef enum {
    WAKE_LOCK_PARTIAL       = 0,  /* CPU on, displays can sleep */
    WAKE_LOCK_FULL          = 1,  /* CPU + displays on */
    WAKE_LOCK_PROXIMITY     = 2,  /* Wake on proximity (headset on) */
} wake_lock_type_t;

/* Battery info */
typedef struct {
    uint32_t voltage_mv;        /* Voltage (millivolts) */
    int32_t current_ma;         /* Current (milliamps, negative = charging) */
    uint32_t capacity_mah;      /* Design capacity */
    uint32_t remaining_mah;     /* Remaining capacity */
    uint8_t soc_percent;        /* State of Charge (0-100%) */
    uint8_t health_percent;     /* Battery health (0-100%) */
    
    bool charging;
    bool full;
    
    int16_t temperature_c;      /* Temperature (Celsius) */
    
    uint32_t cycles;            /* Charge cycles */
    
} battery_info_t;

/* Thermal zone */
typedef struct {
    char name[32];
    bool active;
    
    int16_t temperature_c;      /* Current temp */
    int16_t max_temp_c;         /* Max safe temp */
    
    thermal_level_t level;
    
    /* Trip points */
    int16_t trip_warm;
    int16_t trip_hot;
    int16_t trip_critical;
    int16_t trip_emergency;
    
    /* Statistics */
    uint64_t throttle_events;
    
} thermal_zone_t;

/* Wake lock */
typedef struct {
    uint32_t lock_id;
    bool active;
    
    char name[32];
    wake_lock_type_t type;
    
    uint64_t acquire_time;
    uint32_t timeout_ms;        /* 0 = no timeout */
    
} wake_lock_t;

/* Power domain */
typedef struct {
    char name[32];
    bool enabled;
    
    uint32_t voltage_mv;        /* Operating voltage */
    uint32_t current_ma;        /* Current draw */
    
    uint64_t on_time_ms;        /* Total time enabled */
    uint64_t last_enable_time;
    
} power_domain_t;

/* CPU frequency table */
typedef struct {
    uint32_t freq_mhz;
    uint32_t voltage_mv;
    uint32_t power_mw;          /* Estimated power consumption */
} cpu_freq_entry_t;

/* Power statistics */
typedef struct {
    uint64_t time_active_ms;
    uint64_t time_idle_ms;
    uint64_t time_sleep_ms;
    uint64_t time_deep_sleep_ms;
    
    uint64_t state_transitions;
    uint64_t thermal_throttles;
    
    uint32_t avg_power_mw;      /* Average power consumption */
    uint32_t peak_power_mw;     /* Peak power consumption */
    
} power_stats_t;

/* Power manager */
typedef struct {
    /* Current state */
    power_state_t state;
    cpu_freq_level_t cpu_freq;
    
    /* Battery */
    battery_info_t battery;
    
    /* Thermal zones */
    thermal_zone_t thermal_zones[MAX_THERMAL_ZONES];
    uint32_t num_thermal_zones;
    
    /* Wake locks */
    wake_lock_t wake_locks[MAX_WAKE_LOCKS];
    uint32_t num_wake_locks;
    uint32_t next_lock_id;
    
    /* Power domains */
    power_domain_t domains[MAX_POWER_DOMAINS];
    uint32_t num_domains;
    
    /* CPU frequency table */
    cpu_freq_entry_t freq_table[5];
    
    /* Flags */
    bool thermal_throttling;
    bool low_battery_mode;
    bool charging_enabled;
    
    /* Statistics */
    power_stats_t stats;
    
    /* Last update times */
    uint64_t last_battery_update;
    uint64_t last_thermal_update;
    uint64_t state_enter_time;
    
    volatile uint32_t lock;
    
} power_manager_t;

/* Global state */
static power_manager_t g_pm;

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

/* Initialize CPU frequency table */
static void init_freq_table(void)
{
    /* Frequency table for big cores */
    g_pm.freq_table[CPU_FREQ_ULTRA_LOW] = (cpu_freq_entry_t){ 600, 700, 500 };
    g_pm.freq_table[CPU_FREQ_LOW]       = (cpu_freq_entry_t){ 1200, 800, 1200 };
    g_pm.freq_table[CPU_FREQ_MEDIUM]    = (cpu_freq_entry_t){ 1800, 900, 2500 };
    g_pm.freq_table[CPU_FREQ_HIGH]      = (cpu_freq_entry_t){ 2400, 1000, 4500 };
    g_pm.freq_table[CPU_FREQ_TURBO]     = (cpu_freq_entry_t){ 3000, 1100, 7000 };
}

/* Set CPU frequency */
static void set_cpu_frequency(cpu_freq_level_t level)
{
    if (level >= 5) return;
    
    cpu_freq_entry_t *entry = &g_pm.freq_table[level];
    
    /* TODO: Write to CPU frequency/voltage registers */
    /* On ARM64, this involves SCMI or direct register access */
    
    g_pm.cpu_freq = level;
    
    uart_puts("[PWR] CPU frequency set to ");
    uart_put_dec(entry->freq_mhz);
    uart_puts(" MHz\n");
}

/* Initialize power manager */
void power_init(void)
{
    uart_puts("[PWR] Initializing power management\n");
    
    memset(&g_pm, 0, sizeof(power_manager_t));
    
    g_pm.state = POWER_STATE_ACTIVE;
    g_pm.cpu_freq = CPU_FREQ_HIGH;
    g_pm.charging_enabled = true;
    g_pm.next_lock_id = 1;
    
    /* Initialize frequency table */
    init_freq_table();
    set_cpu_frequency(CPU_FREQ_HIGH);
    
    /* Initialize battery info */
    g_pm.battery.capacity_mah = 5000;  /* 5000 mAh battery */
    g_pm.battery.remaining_mah = 5000;
    g_pm.battery.soc_percent = 100;
    g_pm.battery.health_percent = 100;
    g_pm.battery.voltage_mv = 4200;    /* Fully charged Li-ion */
    
    /* Add thermal zones */
    thermal_zone_t *tz;
    
    /* CPU thermal zone */
    tz = &g_pm.thermal_zones[g_pm.num_thermal_zones++];
    memcpy(tz->name, "CPU", 4);
    tz->active = true;
    tz->max_temp_c = 90;
    tz->trip_warm = 60;
    tz->trip_hot = 70;
    tz->trip_critical = 80;
    tz->trip_emergency = 90;
    
    /* GPU thermal zone */
    tz = &g_pm.thermal_zones[g_pm.num_thermal_zones++];
    memcpy(tz->name, "GPU", 4);
    tz->active = true;
    tz->max_temp_c = 85;
    tz->trip_warm = 55;
    tz->trip_hot = 65;
    tz->trip_critical = 75;
    tz->trip_emergency = 85;
    
    /* Display thermal zone */
    tz = &g_pm.thermal_zones[g_pm.num_thermal_zones++];
    memcpy(tz->name, "Display", 8);
    tz->active = true;
    tz->max_temp_c = 70;
    tz->trip_warm = 45;
    tz->trip_hot = 55;
    tz->trip_critical = 65;
    tz->trip_emergency = 70;
    
    /* Battery thermal zone */
    tz = &g_pm.thermal_zones[g_pm.num_thermal_zones++];
    memcpy(tz->name, "Battery", 8);
    tz->active = true;
    tz->max_temp_c = 60;
    tz->trip_warm = 35;
    tz->trip_hot = 45;
    tz->trip_critical = 55;
    tz->trip_emergency = 60;
    
    /* Add power domains */
    power_domain_t *pd;
    
    pd = &g_pm.domains[g_pm.num_domains++];
    memcpy(pd->name, "Display", 8);
    pd->enabled = true;
    pd->voltage_mv = 3300;
    pd->current_ma = 1500;  /* High power for dual OLED */
    
    pd = &g_pm.domains[g_pm.num_domains++];
    memcpy(pd->name, "Cameras", 8);
    pd->enabled = true;
    pd->voltage_mv = 2800;
    pd->current_ma = 800;
    
    pd = &g_pm.domains[g_pm.num_domains++];
    memcpy(pd->name, "IMU", 4);
    pd->enabled = true;
    pd->voltage_mv = 1800;
    pd->current_ma = 50;
    
    g_pm.state_enter_time = get_timer_count();
    
    uart_puts("[PWR] Power management initialized\n");
}

/* Update battery status */
void power_update_battery(void)
{
    spinlock_acquire(&g_pm.lock);
    
    /* TODO: Read from battery fuel gauge (I2C/SMBus) */
    /* This would read real values from hardware */
    
    /* Simulate battery drain */
    if (!g_pm.battery.charging && g_pm.battery.remaining_mah > 0) {
        /* Estimate drain based on power consumption */
        uint32_t drain_ma = 2000;  /* ~2A average for XR headset */
        uint64_t elapsed_ms = get_timer_count() - g_pm.last_battery_update;
        
        uint32_t drain_mah = (drain_ma * elapsed_ms) / 3600000;
        
        if (drain_mah < g_pm.battery.remaining_mah) {
            g_pm.battery.remaining_mah -= drain_mah;
        } else {
            g_pm.battery.remaining_mah = 0;
        }
        
        g_pm.battery.soc_percent = (g_pm.battery.remaining_mah * 100) / g_pm.battery.capacity_mah;
        
        /* Voltage decreases as battery drains */
        g_pm.battery.voltage_mv = 3000 + (g_pm.battery.soc_percent * 12);  /* 3.0V - 4.2V */
    }
    
    /* Check low battery */
    if (g_pm.battery.soc_percent < 15 && !g_pm.low_battery_mode) {
        g_pm.low_battery_mode = true;
        
        uart_puts("[PWR] ⚠️  Low battery mode activated\n");
        
        /* Reduce performance */
        set_cpu_frequency(CPU_FREQ_LOW);
    }
    
    /* Critical battery */
    if (g_pm.battery.soc_percent < 5) {
        uart_puts("[PWR] ⚠️  CRITICAL BATTERY - Entering sleep mode\n");
        power_set_state(POWER_STATE_SLEEP);
    }
    
    g_pm.last_battery_update = get_timer_count();
    
    spinlock_release(&g_pm.lock);
}

/* Update thermal zones */
void power_update_thermal(void)
{
    spinlock_acquire(&g_pm.lock);
    
    bool needs_throttle = false;
    
    for (uint32_t i = 0; i < g_pm.num_thermal_zones; i++) {
        thermal_zone_t *tz = &g_pm.thermal_zones[i];
        if (!tz->active) continue;
        
        /* TODO: Read from temperature sensors */
        /* This would read real values from hardware */
        
        /* Simulate temperature (would be real sensor values) */
        tz->temperature_c = 35 + (g_pm.cpu_freq * 10);  /* Rough estimate */
        
        /* Determine thermal level */
        if (tz->temperature_c >= tz->trip_emergency) {
            tz->level = THERMAL_EMERGENCY;
            needs_throttle = true;
            
            uart_puts("[PWR] ⚠️  EMERGENCY: ");
            uart_puts(tz->name);
            uart_puts(" at ");
            uart_put_dec(tz->temperature_c);
            uart_puts("°C\n");
            
        } else if (tz->temperature_c >= tz->trip_critical) {
            tz->level = THERMAL_CRITICAL;
            needs_throttle = true;
            
        } else if (tz->temperature_c >= tz->trip_hot) {
            tz->level = THERMAL_HOT;
            needs_throttle = true;
            
        } else if (tz->temperature_c >= tz->trip_warm) {
            tz->level = THERMAL_WARM;
            
        } else {
            tz->level = THERMAL_NORMAL;
        }
    }
    
    /* Apply thermal throttling */
    if (needs_throttle && !g_pm.thermal_throttling) {
        g_pm.thermal_throttling = true;
        
        uart_puts("[PWR] Thermal throttling activated\n");
        
        /* Reduce CPU frequency */
        if (g_pm.cpu_freq > CPU_FREQ_LOW) {
            set_cpu_frequency(g_pm.cpu_freq - 1);
        }
        
        g_pm.stats.thermal_throttles++;
    } else if (!needs_throttle && g_pm.thermal_throttling) {
        g_pm.thermal_throttling = false;
        
        uart_puts("[PWR] Thermal throttling deactivated\n");
    }
    
    g_pm.last_thermal_update = get_timer_count();
    
    spinlock_release(&g_pm.lock);
}

/* Set power state */
void power_set_state(power_state_t state)
{
    spinlock_acquire(&g_pm.lock);
    
    /* Check wake locks */
    bool has_full_wake_lock = false;
    bool has_partial_wake_lock = false;
    
    for (uint32_t i = 0; i < g_pm.num_wake_locks; i++) {
        if (g_pm.wake_locks[i].active) {
            if (g_pm.wake_locks[i].type == WAKE_LOCK_FULL) {
                has_full_wake_lock = true;
            } else if (g_pm.wake_locks[i].type == WAKE_LOCK_PARTIAL) {
                has_partial_wake_lock = true;
            }
        }
    }
    
    /* Can't sleep with active wake locks */
    if (has_full_wake_lock && (state == POWER_STATE_SLEEP || state == POWER_STATE_DEEP_SLEEP)) {
        spinlock_release(&g_pm.lock);
        return;
    }
    
    if (state == g_pm.state) {
        spinlock_release(&g_pm.lock);
        return;
    }
    
    /* Update statistics */
    uint64_t time_in_state = get_timer_count() - g_pm.state_enter_time;
    
    switch (g_pm.state) {
        case POWER_STATE_ACTIVE:     g_pm.stats.time_active_ms += time_in_state; break;
        case POWER_STATE_IDLE:       g_pm.stats.time_idle_ms += time_in_state; break;
        case POWER_STATE_SLEEP:      g_pm.stats.time_sleep_ms += time_in_state; break;
        case POWER_STATE_DEEP_SLEEP: g_pm.stats.time_deep_sleep_ms += time_in_state; break;
        default: break;
    }
    
    uart_puts("[PWR] State transition: ");
    
    /* Exit current state */
    switch (g_pm.state) {
        case POWER_STATE_ACTIVE:
            uart_puts("ACTIVE");
            break;
        case POWER_STATE_IDLE:
            uart_puts("IDLE");
            break;
        case POWER_STATE_SLEEP:
            uart_puts("SLEEP");
            break;
        case POWER_STATE_DEEP_SLEEP:
            uart_puts("DEEP_SLEEP");
            break;
        default:
            break;
    }
    
    uart_puts(" → ");
    
    /* Enter new state */
    switch (state) {
        case POWER_STATE_ACTIVE:
            uart_puts("ACTIVE\n");
            set_cpu_frequency(CPU_FREQ_HIGH);
            /* Enable displays, cameras, etc */
            break;
            
        case POWER_STATE_IDLE:
            uart_puts("IDLE\n");
            set_cpu_frequency(CPU_FREQ_MEDIUM);
            /* Keep displays on but reduce refresh rate */
            break;
            
        case POWER_STATE_SLEEP:
            uart_puts("SLEEP\n");
            set_cpu_frequency(CPU_FREQ_LOW);
            /* Disable displays, pause tracking */
            break;
            
        case POWER_STATE_DEEP_SLEEP:
            uart_puts("DEEP_SLEEP\n");
            set_cpu_frequency(CPU_FREQ_ULTRA_LOW);
            /* Disable everything except wake sources */
            break;
            
        default:
            break;
    }
    
    g_pm.state = state;
    g_pm.state_enter_time = get_timer_count();
    g_pm.stats.state_transitions++;
    
    spinlock_release(&g_pm.lock);
}

/* Acquire wake lock */
uint32_t power_acquire_wake_lock(const char *name, wake_lock_type_t type, uint32_t timeout_ms)
{
    spinlock_acquire(&g_pm.lock);
    
    /* Find free slot */
    wake_lock_t *lock = NULL;
    for (uint32_t i = 0; i < MAX_WAKE_LOCKS; i++) {
        if (!g_pm.wake_locks[i].active) {
            lock = &g_pm.wake_locks[i];
            break;
        }
    }
    
    if (!lock) {
        spinlock_release(&g_pm.lock);
        return 0;
    }
    
    lock->lock_id = g_pm.next_lock_id++;
    lock->active = true;
    lock->type = type;
    lock->acquire_time = get_timer_count();
    lock->timeout_ms = timeout_ms;
    memcpy(lock->name, name, 31);
    
    g_pm.num_wake_locks++;
    
    uint32_t lock_id = lock->lock_id;
    
    spinlock_release(&g_pm.lock);
    
    uart_puts("[PWR] Wake lock acquired: ");
    uart_puts(name);
    uart_puts("\n");
    
    return lock_id;
}

/* Release wake lock */
void power_release_wake_lock(uint32_t lock_id)
{
    spinlock_acquire(&g_pm.lock);
    
    for (uint32_t i = 0; i < MAX_WAKE_LOCKS; i++) {
        if (g_pm.wake_locks[i].active && g_pm.wake_locks[i].lock_id == lock_id) {
            g_pm.wake_locks[i].active = false;
            g_pm.num_wake_locks--;
            
            uart_puts("[PWR] Wake lock released: ");
            uart_puts(g_pm.wake_locks[i].name);
            uart_puts("\n");
            
            break;
        }
    }
    
    spinlock_release(&g_pm.lock);
}

/* Get battery info */
void power_get_battery_info(battery_info_t *info)
{
    spinlock_acquire(&g_pm.lock);
    memcpy(info, &g_pm.battery, sizeof(battery_info_t));
    spinlock_release(&g_pm.lock);
}

/* Print statistics */
void power_print_stats(void)
{
    uart_puts("\n[PWR] Power Management Statistics:\n");
    uart_puts("════════════════════════════════════════\n");
    
    uart_puts("  Current state:     ");
    switch (g_pm.state) {
        case POWER_STATE_ACTIVE:     uart_puts("ACTIVE\n"); break;
        case POWER_STATE_IDLE:       uart_puts("IDLE\n"); break;
        case POWER_STATE_SLEEP:      uart_puts("SLEEP\n"); break;
        case POWER_STATE_DEEP_SLEEP: uart_puts("DEEP_SLEEP\n"); break;
        default:                     uart_puts("UNKNOWN\n"); break;
    }
    
    uart_puts("  CPU frequency:     ");
    uart_put_dec(g_pm.freq_table[g_pm.cpu_freq].freq_mhz);
    uart_puts(" MHz\n");
    
    uart_puts("\n  Battery:\n");
    uart_puts("    Charge:          ");
    uart_put_dec(g_pm.battery.soc_percent);
    uart_puts("%\n");
    
    uart_puts("    Voltage:         ");
    uart_put_dec(g_pm.battery.voltage_mv);
    uart_puts(" mV\n");
    
    uart_puts("    Capacity:        ");
    uart_put_dec(g_pm.battery.remaining_mah);
    uart_puts(" / ");
    uart_put_dec(g_pm.battery.capacity_mah);
    uart_puts(" mAh\n");
    
    uart_puts("    Charging:        ");
    uart_puts(g_pm.battery.charging ? "Yes" : "No");
    uart_puts("\n");
    
    uart_puts("\n  Thermal:\n");
    for (uint32_t i = 0; i < g_pm.num_thermal_zones; i++) {
        thermal_zone_t *tz = &g_pm.thermal_zones[i];
        if (!tz->active) continue;
        
        uart_puts("    ");
        uart_puts(tz->name);
        uart_puts(": ");
        uart_put_dec(tz->temperature_c);
        uart_puts("°C");
        
        if (tz->level != THERMAL_NORMAL) {
            uart_puts(" [");
            switch (tz->level) {
                case THERMAL_WARM:     uart_puts("WARM"); break;
                case THERMAL_HOT:      uart_puts("HOT"); break;
                case THERMAL_CRITICAL: uart_puts("CRITICAL"); break;
                case THERMAL_EMERGENCY:uart_puts("EMERGENCY"); break;
                default: break;
            }
            uart_puts("]");
        }
        uart_puts("\n");
    }
    
    uart_puts("\n  Wake locks:        ");
    uart_put_dec(g_pm.num_wake_locks);
    uart_puts("\n");
    
    uart_puts("  State transitions: ");
    uart_put_dec(g_pm.stats.state_transitions);
    uart_puts("\n");
    
    uart_puts("  Thermal throttles: ");
    uart_put_dec(g_pm.stats.thermal_throttles);
    uart_puts("\n");
    
    uart_puts("\n");
}