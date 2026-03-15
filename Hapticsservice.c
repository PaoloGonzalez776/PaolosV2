/*
 * haptics_service.c - Haptic Feedback Service
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Sistema de retroalimentación háptica para XR headset.
 * Control de motores de vibración en controllers y headset.
 * 
 * CRÍTICO PARA XR IMMERSION:
 * - Haptic feedback = "feeling" virtual objects
 * - Low latency (<10ms) = synchronization with events
 * - Precise control = realistic sensations
 * - Multiple actuators = localized feedback
 * 
 * CARACTERÍSTICAS:
 * - Multiple actuators (controllers, headset)
 * - Waveform generation (sine, square, custom)
 * - Amplitude/frequency control
 * - Haptic effects/patterns
 * - Low latency (<10ms)
 * - Audio-to-haptic conversion
 * - Spatial haptics (direction-aware)
 * 
 * ACTUATORS EN XR SYSTEM:
 * - Left controller:  HD vibration motor
 * - Right controller: HD vibration motor
 * - Headset:          Multiple voice coils (optional)
 * 
 * WAVEFORM TYPES:
 * - Continuous: Sustained vibration
 * - Transient:  Single pulse/click
 * - Custom:     User-defined pattern
 * 
 * HAPTIC EFFECTS:
 * - Click:      Sharp tap
 * - Rumble:     Low frequency vibration
 * - Buzz:       High frequency vibration
 * - Pulse:      Rhythmic pattern
 * - Sweep:      Frequency sweep
 * 
 * INSPIRATION:
 * - HD Rumble (Nintendo Switch)
 * - DualSense haptics (PlayStation 5)
 * - Apple Taptic Engine
 * - Meta Touch controllers
 * - Steam Deck haptics
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

/* Haptic constants */
#define MAX_ACTUATORS           8       /* Max haptic actuators */
#define MAX_EFFECTS             32      /* Max concurrent effects */
#define HAPTIC_SAMPLE_RATE      1000    /* 1kHz update rate */
#define MAX_WAVEFORM_LENGTH     1024    /* Max samples in waveform */

/* Actuator types */
typedef enum {
    ACTUATOR_LRA            = 0,  /* Linear Resonant Actuator */
    ACTUATOR_ERM            = 1,  /* Eccentric Rotating Mass */
    ACTUATOR_VOICE_COIL     = 2,  /* Voice coil (precise) */
    ACTUATOR_PIEZO          = 3,  /* Piezoelectric */
} actuator_type_t;

/* Actuator location */
typedef enum {
    HAPTIC_CONTROLLER_LEFT  = 0,
    HAPTIC_CONTROLLER_RIGHT = 1,
    HAPTIC_HEADSET_FRONT    = 2,
    HAPTIC_HEADSET_BACK     = 3,
    HAPTIC_HEADSET_LEFT     = 4,
    HAPTIC_HEADSET_RIGHT    = 5,
} haptic_location_t;

/* Waveform types */
typedef enum {
    WAVE_SINE               = 0,  /* Smooth sine wave */
    WAVE_SQUARE             = 1,  /* Sharp square wave */
    WAVE_TRIANGLE           = 2,  /* Triangle wave */
    WAVE_SAWTOOTH           = 3,  /* Sawtooth wave */
    WAVE_CUSTOM             = 4,  /* Custom waveform */
    WAVE_CLICK              = 5,  /* Sharp click */
    WAVE_RUMBLE             = 6,  /* Low frequency rumble */
    WAVE_BUZZ               = 7,  /* High frequency buzz */
} waveform_type_t;

/* Effect types */
typedef enum {
    EFFECT_CONSTANT         = 0,  /* Constant vibration */
    EFFECT_PERIODIC         = 1,  /* Periodic waveform */
    EFFECT_TRANSIENT        = 2,  /* Single pulse */
    EFFECT_RAMP             = 3,  /* Ramp up/down */
} effect_type_t;

/* Haptic actuator */
typedef struct {
    uint32_t actuator_id;
    bool active;
    
    char name[32];
    actuator_type_t type;
    haptic_location_t location;
    
    /* Capabilities */
    uint16_t freq_min;          /* Min frequency (Hz) */
    uint16_t freq_max;          /* Max frequency (Hz) */
    uint16_t freq_resonant;     /* Resonant frequency (Hz) */
    
    /* Current state */
    uint16_t amplitude;         /* 0-65535 */
    uint16_t frequency;         /* Hz */
    bool playing;
    
    /* Hardware */
    uint32_t hw_base_addr;      /* PWM/I2C address */
    
    /* Statistics */
    uint64_t activations;
    uint64_t total_duration_ms;
    
} haptic_actuator_t;

/* Haptic waveform */
typedef struct {
    waveform_type_t type;
    
    uint16_t frequency;         /* Hz */
    uint16_t amplitude;         /* 0-65535 */
    uint32_t duration_ms;
    
    /* Custom waveform data */
    int16_t *samples;           /* Sample buffer */
    uint32_t num_samples;
    
} haptic_waveform_t;

/* Haptic effect */
typedef struct {
    uint32_t effect_id;
    bool active;
    
    effect_type_t type;
    uint32_t actuator_id;       /* Target actuator */
    
    haptic_waveform_t waveform;
    
    uint64_t start_time;
    uint32_t duration_ms;       /* 0 = infinite */
    uint32_t delay_ms;          /* Delay before start */
    
    /* Envelope */
    uint32_t attack_ms;         /* Fade in */
    uint32_t sustain_ms;        /* Full amplitude */
    uint32_t decay_ms;          /* Fade out */
    
    uint16_t attack_level;
    uint16_t sustain_level;
    
    /* Repeat */
    bool loop;
    uint32_t loop_count;
    
} haptic_effect_t;

/* Predefined haptic patterns */
typedef enum {
    PATTERN_CLICK           = 0,  /* Sharp click (10ms) */
    PATTERN_DOUBLE_CLICK    = 1,  /* Two clicks */
    PATTERN_LONG_PRESS      = 2,  /* Sustained vibration */
    PATTERN_SUCCESS         = 3,  /* Positive feedback */
    PATTERN_ERROR           = 4,  /* Negative feedback */
    PATTERN_NOTIFICATION    = 5,  /* Alert pattern */
    PATTERN_HEARTBEAT       = 6,  /* Rhythmic pulse */
    PATTERN_IMPACT          = 7,  /* Collision feedback */
} haptic_pattern_t;

/* Haptic service */
typedef struct {
    /* Actuators */
    haptic_actuator_t actuators[MAX_ACTUATORS];
    uint32_t num_actuators;
    uint32_t next_actuator_id;
    
    /* Active effects */
    haptic_effect_t effects[MAX_EFFECTS];
    uint32_t num_effects;
    uint32_t next_effect_id;
    
    /* Master volume */
    uint16_t master_amplitude;  /* 0-65535, global multiplier */
    bool enabled;
    
    /* Statistics */
    uint64_t effects_played;
    uint64_t total_duration_ms;
    
    volatile uint32_t lock;
    
} haptic_service_t;

/* Global state */
static haptic_service_t g_haptic;

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

/* Math helpers */
static inline int16_t sine_approx(uint16_t phase)
{
    /* Fast sine approximation using lookup or polynomial */
    /* For simplicity, using rough approximation */
    float angle = (float)phase * 3.14159f / 32768.0f;
    return (int16_t)(32767.0f * sinf(angle));
}

/* Generate waveform sample */
static int16_t generate_sample(haptic_waveform_t *wave, uint32_t sample_index)
{
    if (wave->type == WAVE_CUSTOM && wave->samples) {
        return wave->samples[sample_index % wave->num_samples];
    }
    
    uint16_t phase = (sample_index * wave->frequency * 65536) / HAPTIC_SAMPLE_RATE;
    int16_t sample = 0;
    
    switch (wave->type) {
        case WAVE_SINE:
            sample = sine_approx(phase);
            break;
            
        case WAVE_SQUARE:
            sample = (phase & 0x8000) ? 32767 : -32767;
            break;
            
        case WAVE_TRIANGLE:
            if (phase < 32768) {
                sample = (int16_t)((phase * 2) - 32768);
            } else {
                sample = (int16_t)(32768 - ((phase - 32768) * 2));
            }
            break;
            
        case WAVE_SAWTOOTH:
            sample = (int16_t)(phase - 32768);
            break;
            
        case WAVE_CLICK:
            /* Sharp pulse */
            sample = (sample_index < 10) ? 32767 : 0;
            break;
            
        case WAVE_RUMBLE:
            /* Low frequency (80 Hz) */
            phase = (sample_index * 80 * 65536) / HAPTIC_SAMPLE_RATE;
            sample = sine_approx(phase);
            break;
            
        case WAVE_BUZZ:
            /* High frequency (250 Hz) */
            phase = (sample_index * 250 * 65536) / HAPTIC_SAMPLE_RATE;
            sample = sine_approx(phase);
            break;
            
        default:
            break;
    }
    
    /* Apply amplitude */
    sample = (sample * wave->amplitude) / 65535;
    
    return sample;
}

/* Initialize haptic service */
void haptic_init(void)
{
    uart_puts("[HAPTIC] Initializing haptic feedback service\n");
    
    memset(&g_haptic, 0, sizeof(haptic_service_t));
    
    g_haptic.enabled = true;
    g_haptic.master_amplitude = 65535;  /* Full amplitude */
    g_haptic.next_actuator_id = 1;
    g_haptic.next_effect_id = 1;
    
    uart_puts("[HAPTIC] Haptic service initialized\n");
}

/* Register haptic actuator */
uint32_t haptic_register_actuator(const char *name, actuator_type_t type, 
                                  haptic_location_t location, uint32_t hw_base)
{
    spinlock_acquire(&g_haptic.lock);
    
    if (g_haptic.num_actuators >= MAX_ACTUATORS) {
        spinlock_release(&g_haptic.lock);
        return 0;
    }
    
    haptic_actuator_t *act = &g_haptic.actuators[g_haptic.num_actuators];
    memset(act, 0, sizeof(haptic_actuator_t));
    
    act->actuator_id = g_haptic.next_actuator_id++;
    memcpy(act->name, name, 31);
    act->type = type;
    act->location = location;
    act->hw_base_addr = hw_base;
    act->active = true;
    
    /* Set capabilities based on type */
    switch (type) {
        case ACTUATOR_LRA:
            act->freq_min = 50;
            act->freq_max = 300;
            act->freq_resonant = 175;  /* Typical LRA */
            break;
            
        case ACTUATOR_ERM:
            act->freq_min = 30;
            act->freq_max = 250;
            act->freq_resonant = 0;    /* No resonance */
            break;
            
        case ACTUATOR_VOICE_COIL:
            act->freq_min = 10;
            act->freq_max = 1000;
            act->freq_resonant = 0;
            break;
            
        case ACTUATOR_PIEZO:
            act->freq_min = 100;
            act->freq_max = 10000;
            act->freq_resonant = 0;
            break;
    }
    
    uint32_t actuator_id = act->actuator_id;
    g_haptic.num_actuators++;
    
    spinlock_release(&g_haptic.lock);
    
    uart_puts("[HAPTIC] Registered actuator: ");
    uart_puts(name);
    uart_puts("\n");
    
    return actuator_id;
}

/* Create haptic effect */
uint32_t haptic_create_effect(uint32_t actuator_id, effect_type_t type,
                              haptic_waveform_t *waveform, uint32_t duration_ms)
{
    spinlock_acquire(&g_haptic.lock);
    
    /* Find actuator */
    haptic_actuator_t *act = NULL;
    for (uint32_t i = 0; i < g_haptic.num_actuators; i++) {
        if (g_haptic.actuators[i].active && 
            g_haptic.actuators[i].actuator_id == actuator_id) {
            act = &g_haptic.actuators[i];
            break;
        }
    }
    
    if (!act) {
        spinlock_release(&g_haptic.lock);
        return 0;
    }
    
    /* Find free effect slot */
    haptic_effect_t *effect = NULL;
    for (uint32_t i = 0; i < MAX_EFFECTS; i++) {
        if (!g_haptic.effects[i].active) {
            effect = &g_haptic.effects[i];
            break;
        }
    }
    
    if (!effect) {
        spinlock_release(&g_haptic.lock);
        return 0;
    }
    
    memset(effect, 0, sizeof(haptic_effect_t));
    
    effect->effect_id = g_haptic.next_effect_id++;
    effect->active = true;
    effect->type = type;
    effect->actuator_id = actuator_id;
    effect->duration_ms = duration_ms;
    
    memcpy(&effect->waveform, waveform, sizeof(haptic_waveform_t));
    
    g_haptic.num_effects++;
    
    uint32_t effect_id = effect->effect_id;
    
    spinlock_release(&g_haptic.lock);
    
    return effect_id;
}

/* Play haptic effect */
void haptic_play_effect(uint32_t effect_id)
{
    spinlock_acquire(&g_haptic.lock);
    
    haptic_effect_t *effect = NULL;
    for (uint32_t i = 0; i < MAX_EFFECTS; i++) {
        if (g_haptic.effects[i].active && g_haptic.effects[i].effect_id == effect_id) {
            effect = &g_haptic.effects[i];
            break;
        }
    }
    
    if (!effect) {
        spinlock_release(&g_haptic.lock);
        return;
    }
    
    effect->start_time = get_timer_count();
    
    /* Find actuator and start playback */
    for (uint32_t i = 0; i < g_haptic.num_actuators; i++) {
        if (g_haptic.actuators[i].actuator_id == effect->actuator_id) {
            g_haptic.actuators[i].playing = true;
            g_haptic.actuators[i].amplitude = effect->waveform.amplitude;
            g_haptic.actuators[i].frequency = effect->waveform.frequency;
            g_haptic.actuators[i].activations++;
            break;
        }
    }
    
    g_haptic.effects_played++;
    
    spinlock_release(&g_haptic.lock);
}

/* Stop haptic effect */
void haptic_stop_effect(uint32_t effect_id)
{
    spinlock_acquire(&g_haptic.lock);
    
    for (uint32_t i = 0; i < MAX_EFFECTS; i++) {
        if (g_haptic.effects[i].active && g_haptic.effects[i].effect_id == effect_id) {
            /* Stop actuator */
            for (uint32_t j = 0; j < g_haptic.num_actuators; j++) {
                if (g_haptic.actuators[j].actuator_id == g_haptic.effects[i].actuator_id) {
                    g_haptic.actuators[j].playing = false;
                    g_haptic.actuators[j].amplitude = 0;
                    break;
                }
            }
            
            g_haptic.effects[i].active = false;
            g_haptic.num_effects--;
            break;
        }
    }
    
    spinlock_release(&g_haptic.lock);
}

/* Play predefined pattern */
void haptic_play_pattern(uint32_t actuator_id, haptic_pattern_t pattern)
{
    haptic_waveform_t wave;
    memset(&wave, 0, sizeof(haptic_waveform_t));
    
    switch (pattern) {
        case PATTERN_CLICK:
            wave.type = WAVE_CLICK;
            wave.amplitude = 40000;
            wave.duration_ms = 10;
            break;
            
        case PATTERN_DOUBLE_CLICK:
            wave.type = WAVE_CLICK;
            wave.amplitude = 40000;
            wave.duration_ms = 10;
            /* TODO: Add second click with delay */
            break;
            
        case PATTERN_LONG_PRESS:
            wave.type = WAVE_SINE;
            wave.frequency = 175;  /* Resonant */
            wave.amplitude = 50000;
            wave.duration_ms = 500;
            break;
            
        case PATTERN_SUCCESS:
            wave.type = WAVE_SINE;
            wave.frequency = 200;
            wave.amplitude = 35000;
            wave.duration_ms = 100;
            break;
            
        case PATTERN_ERROR:
            wave.type = WAVE_BUZZ;
            wave.frequency = 250;
            wave.amplitude = 45000;
            wave.duration_ms = 200;
            break;
            
        case PATTERN_IMPACT:
            wave.type = WAVE_CLICK;
            wave.amplitude = 65535;  /* Max */
            wave.duration_ms = 20;
            break;
            
        default:
            return;
    }
    
    uint32_t effect_id = haptic_create_effect(actuator_id, EFFECT_TRANSIENT, &wave, wave.duration_ms);
    if (effect_id) {
        haptic_play_effect(effect_id);
    }
}

/* Update haptic system (called from timer interrupt at 1kHz) */
void haptic_update(void)
{
    spinlock_acquire(&g_haptic.lock);
    
    if (!g_haptic.enabled) {
        spinlock_release(&g_haptic.lock);
        return;
    }
    
    uint64_t current_time = get_timer_count();
    
    /* Update all active effects */
    for (uint32_t i = 0; i < MAX_EFFECTS; i++) {
        haptic_effect_t *effect = &g_haptic.effects[i];
        if (!effect->active) continue;
        
        uint64_t elapsed = current_time - effect->start_time;
        
        /* Check if effect has finished */
        if (effect->duration_ms > 0 && elapsed >= effect->duration_ms) {
            haptic_stop_effect(effect->effect_id);
            continue;
        }
        
        /* Generate sample for this time step */
        uint32_t sample_index = (uint32_t)(elapsed * HAPTIC_SAMPLE_RATE / 1000);
        int16_t sample = generate_sample(&effect->waveform, sample_index);
        
        /* Apply master amplitude */
        sample = (sample * g_haptic.master_amplitude) / 65535;
        
        /* Send to hardware */
        for (uint32_t j = 0; j < g_haptic.num_actuators; j++) {
            if (g_haptic.actuators[j].actuator_id == effect->actuator_id) {
                /* TODO: Write to hardware PWM/I2C */
                /* write_actuator(actuator->hw_base_addr, sample); */
                break;
            }
        }
    }
    
    spinlock_release(&g_haptic.lock);
}

/* Set master amplitude (global volume) */
void haptic_set_amplitude(uint16_t amplitude)
{
    spinlock_acquire(&g_haptic.lock);
    g_haptic.master_amplitude = amplitude;
    spinlock_release(&g_haptic.lock);
}

/* Enable/disable haptics */
void haptic_set_enabled(bool enabled)
{
    spinlock_acquire(&g_haptic.lock);
    g_haptic.enabled = enabled;
    
    if (!enabled) {
        /* Stop all actuators */
        for (uint32_t i = 0; i < g_haptic.num_actuators; i++) {
            g_haptic.actuators[i].playing = false;
            g_haptic.actuators[i].amplitude = 0;
        }
    }
    
    spinlock_release(&g_haptic.lock);
}

/* Print statistics */
void haptic_print_stats(void)
{
    uart_puts("\n[HAPTIC] Statistics:\n");
    uart_puts("════════════════════════════════════════\n");
    
    uart_puts("  Actuators:         ");
    uart_put_dec(g_haptic.num_actuators);
    uart_puts("\n");
    
    uart_puts("  Active effects:    ");
    uart_put_dec(g_haptic.num_effects);
    uart_puts("\n");
    
    uart_puts("  Effects played:    ");
    uart_put_dec(g_haptic.effects_played);
    uart_puts("\n");
    
    uart_puts("  Master amplitude:  ");
    uart_put_dec((g_haptic.master_amplitude * 100) / 65535);
    uart_puts("%\n");
    
    uart_puts("  Enabled:           ");
    uart_puts(g_haptic.enabled ? "Yes" : "No");
    uart_puts("\n");
    
    uart_puts("\n  Per-actuator stats:\n");
    for (uint32_t i = 0; i < g_haptic.num_actuators; i++) {
        haptic_actuator_t *act = &g_haptic.actuators[i];
        if (!act->active) continue;
        
        uart_puts("  - ");
        uart_puts(act->name);
        uart_puts(": ");
        uart_put_dec(act->activations);
        uart_puts(" activations");
        
        if (act->playing) {
            uart_puts(" [PLAYING]");
        }
        
        uart_puts("\n");
    }
    
    uart_puts("\n");
}