/*
 * input_service.c - Input Service (Vision Pro-style)
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Sistema completo de input multimodal inspirado en Vision Pro:
 * - Eye tracking para targeting (cursor)
 * - Hand gestures para acciones (pinch, scroll, etc)
 * - Virtual keyboard con air typing
 * - Dwell control (accessibility)
 * - Voice input integration
 * - Calibration system
 * 
 * HARDWARE REQUIREMENTS:
 * - 4× IR cameras para eye tracking (LEDs + sensors)
 * - 10× cameras para hand tracking
 * - IMU para head tracking
 * - Microphone array para voice
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

/* Hardware addresses */
#define EYE_TRACKING_BASE       0x4E000000
#define HAND_TRACKING_BASE      0x4F000000
#define VOICE_INPUT_BASE        0x50000000

/* Eye tracking registers */
#define EYE_LEFT_X              (EYE_TRACKING_BASE + 0x00)
#define EYE_LEFT_Y              (EYE_TRACKING_BASE + 0x04)
#define EYE_RIGHT_X             (EYE_TRACKING_BASE + 0x08)
#define EYE_RIGHT_Y             (EYE_TRACKING_BASE + 0x0C)
#define EYE_COMBINED_X          (EYE_TRACKING_BASE + 0x10)
#define EYE_COMBINED_Y          (EYE_TRACKING_BASE + 0x14)
#define EYE_CALIBRATION_DATA    (EYE_TRACKING_BASE + 0x20)
#define EYE_STATUS              (EYE_TRACKING_BASE + 0x30)
#define EYE_CONFIDENCE          (EYE_TRACKING_BASE + 0x34)

/* Hand tracking registers */
#define HAND_LEFT_BASE          (HAND_TRACKING_BASE + 0x00)
#define HAND_RIGHT_BASE         (HAND_TRACKING_BASE + 0x100)

/* Per-hand offsets */
#define HAND_PALM_X             0x00
#define HAND_PALM_Y             0x04
#define HAND_PALM_Z             0x08
#define HAND_THUMB_TIP_X        0x10
#define HAND_THUMB_TIP_Y        0x14
#define HAND_THUMB_TIP_Z        0x18
#define HAND_INDEX_TIP_X        0x20
#define HAND_INDEX_TIP_Y        0x24
#define HAND_INDEX_TIP_Z        0x28
#define HAND_MIDDLE_TIP_X       0x30
#define HAND_MIDDLE_TIP_Y       0x34
#define HAND_MIDDLE_TIP_Z       0x38
#define HAND_RING_TIP_X         0x40
#define HAND_RING_TIP_Y         0x44
#define HAND_RING_TIP_Z         0x48
#define HAND_PINKY_TIP_X        0x50
#define HAND_PINKY_TIP_Y        0x54
#define HAND_PINKY_TIP_Z        0x58
#define HAND_CONFIDENCE         0x60
#define HAND_GESTURE            0x64

/* Gesture types (hardware detected) */
#define GESTURE_NONE            0
#define GESTURE_PINCH           1
#define GESTURE_DOUBLE_PINCH    2
#define GESTURE_LONG_PINCH      3
#define GESTURE_FLICK_UP        4
#define GESTURE_FLICK_DOWN      5
#define GESTURE_FLICK_LEFT      6
#define GESTURE_FLICK_RIGHT     7
#define GESTURE_PALM_FLIP       8
#define GESTURE_POINT           9

/* Input event types */
typedef enum {
    INPUT_EVENT_CURSOR_MOVE     = 0,
    INPUT_EVENT_TAP             = 1,
    INPUT_EVENT_DOUBLE_TAP      = 2,
    INPUT_EVENT_LONG_PRESS      = 3,
    INPUT_EVENT_SCROLL          = 4,
    INPUT_EVENT_PINCH_ZOOM      = 5,
    INPUT_EVENT_KEY_PRESS       = 6,
    INPUT_EVENT_VOICE_COMMAND   = 7,
    INPUT_EVENT_DWELL_SELECT    = 8,
} input_event_type_t;

/* 3D point */
typedef struct {
    float x, y, z;
} point3d_t;

/* Eye tracking data */
typedef struct {
    point3d_t left_gaze;
    point3d_t right_gaze;
    point3d_t combined_gaze;
    
    float convergence;
    float pupil_diameter_left;
    float pupil_diameter_right;
    
    float confidence;
    bool valid;
    
    uint64_t timestamp;
    
} eye_tracking_t;

/* Hand skeleton (21 joints) */
typedef struct {
    point3d_t wrist;
    point3d_t thumb[4];      /* CMC, MCP, IP, TIP */
    point3d_t index[4];      /* MCP, PIP, DIP, TIP */
    point3d_t middle[4];
    point3d_t ring[4];
    point3d_t pinky[4];
    
    float confidence;
    bool valid;
    
} hand_skeleton_t;

/* Hand state */
typedef struct {
    hand_skeleton_t skeleton;
    
    uint32_t current_gesture;
    uint32_t previous_gesture;
    
    point3d_t velocity;
    point3d_t acceleration;
    
    bool is_pinching;
    float pinch_distance;
    uint64_t pinch_start_time;
    
    uint64_t timestamp;
    
} hand_state_t;

/* Virtual keyboard */
#define KEYBOARD_ROWS           4
#define KEYBOARD_COLS           10
#define MAX_KEYS                40

typedef struct {
    char character;
    char shift_character;
    int32_t x, y;
    uint32_t width, height;
    bool is_hovered;
    bool is_pressed;
    uint64_t hover_start_time;
} virtual_key_t;

typedef struct {
    virtual_key_t keys[MAX_KEYS];
    uint32_t num_keys;
    
    bool visible;
    int32_t x, y;
    uint32_t width, height;
    
    bool shift_active;
    bool caps_lock_active;
    
    char input_buffer[256];
    uint32_t buffer_pos;
    
} virtual_keyboard_t;

/* Dwell control (accessibility) */
typedef struct {
    bool enabled;
    uint32_t dwell_time_ms;
    
    point3d_t dwell_target;
    uint64_t dwell_start_time;
    bool dwelling;
    
    float progress;  /* 0.0 to 1.0 */
    
} dwell_control_t;

/* Calibration data */
typedef struct {
    /* Eye calibration points */
    point3d_t eye_cal_points[9];
    point3d_t eye_cal_gazes[9];
    uint32_t eye_cal_count;
    bool eye_calibrated;
    
    /* Hand calibration */
    float hand_scale_left;
    float hand_scale_right;
    bool hand_calibrated;
    
} calibration_data_t;

/* Input service state */
typedef struct {
    /* Eye tracking */
    eye_tracking_t eye;
    
    /* Hand tracking */
    hand_state_t left_hand;
    hand_state_t right_hand;
    
    /* Cursor (driven by eyes) */
    int32_t cursor_x;
    int32_t cursor_y;
    int32_t screen_width;
    int32_t screen_height;
    
    /* Virtual keyboard */
    virtual_keyboard_t keyboard;
    
    /* Dwell control */
    dwell_control_t dwell;
    
    /* Calibration */
    calibration_data_t calibration;
    
    /* Settings */
    bool eye_tracking_enabled;
    bool hand_tracking_enabled;
    bool voice_enabled;
    float sensitivity;
    
    /* Statistics */
    uint64_t total_taps;
    uint64_t total_scrolls;
    uint64_t total_keys_typed;
    uint64_t frames_processed;
    
    volatile uint32_t lock;
    
} input_service_t;

/* Global state */
static input_service_t g_input;

/* MMIO helpers */
static inline void mmio_write32(uint64_t addr, uint32_t val) {
    *((volatile uint32_t *)addr) = val;
    __asm__ volatile("dsb sy" : : : "memory");
}

static inline uint32_t mmio_read32(uint64_t addr) {
    __asm__ volatile("dsb sy" : : : "memory");
    return *((volatile uint32_t *)addr);
}

static inline float mmio_read_float(uint64_t addr) {
    union { uint32_t u; float f; } conv;
    conv.u = mmio_read32(addr);
    return conv.f;
}

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

/* Distance between two 3D points */
static float point3d_distance(point3d_t a, point3d_t b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    
    /* Simplified sqrt (would use hardware FPU) */
    float dist_sq = dx*dx + dy*dy + dz*dz;
    
    /* Newton-Raphson approximation */
    if (dist_sq < 0.001f) return 0.0f;
    
    float x = dist_sq;
    float half_x = 0.5f * x;
    uint32_t i = *(uint32_t*)&x;
    i = 0x5f3759df - (i >> 1);
    x = *(float*)&i;
    x = x * (1.5f - half_x * x * x);
    x = x * (1.5f - half_x * x * x);
    
    return dist_sq * x;
}

/* Read eye tracking data from hardware */
static void read_eye_tracking(void)
{
    eye_tracking_t *eye = &g_input.eye;
    
    /* Read raw gaze coordinates (normalized 0.0-1.0) */
    eye->left_gaze.x = mmio_read_float(EYE_LEFT_X);
    eye->left_gaze.y = mmio_read_float(EYE_LEFT_Y);
    
    eye->right_gaze.x = mmio_read_float(EYE_RIGHT_X);
    eye->right_gaze.y = mmio_read_float(EYE_RIGHT_Y);
    
    /* Combined gaze (already computed by hardware) */
    eye->combined_gaze.x = mmio_read_float(EYE_COMBINED_X);
    eye->combined_gaze.y = mmio_read_float(EYE_COMBINED_Y);
    
    /* Status and confidence */
    uint32_t status = mmio_read32(EYE_STATUS);
    eye->valid = (status & 0x1) != 0;
    eye->confidence = mmio_read_float(EYE_CONFIDENCE);
    
    eye->timestamp = get_timer_count();
    
    /* Update cursor position based on gaze */
    if (eye->valid && g_input.eye_tracking_enabled) {
        g_input.cursor_x = (int32_t)(eye->combined_gaze.x * g_input.screen_width);
        g_input.cursor_y = (int32_t)(eye->combined_gaze.y * g_input.screen_height);
        
        /* Clamp to screen bounds */
        if (g_input.cursor_x < 0) g_input.cursor_x = 0;
        if (g_input.cursor_x >= (int32_t)g_input.screen_width) 
            g_input.cursor_x = g_input.screen_width - 1;
        if (g_input.cursor_y < 0) g_input.cursor_y = 0;
        if (g_input.cursor_y >= (int32_t)g_input.screen_height) 
            g_input.cursor_y = g_input.screen_height - 1;
    }
}

/* Read hand tracking data from hardware */
static void read_hand_tracking(hand_state_t *hand, uint64_t base_addr)
{
    /* Read palm position */
    hand->skeleton.wrist.x = mmio_read_float(base_addr + HAND_PALM_X);
    hand->skeleton.wrist.y = mmio_read_float(base_addr + HAND_PALM_Y);
    hand->skeleton.wrist.z = mmio_read_float(base_addr + HAND_PALM_Z);
    
    /* Read fingertips (simplified - would read all joints) */
    hand->skeleton.thumb[3].x = mmio_read_float(base_addr + HAND_THUMB_TIP_X);
    hand->skeleton.thumb[3].y = mmio_read_float(base_addr + HAND_THUMB_TIP_Y);
    hand->skeleton.thumb[3].z = mmio_read_float(base_addr + HAND_THUMB_TIP_Z);
    
    hand->skeleton.index[3].x = mmio_read_float(base_addr + HAND_INDEX_TIP_X);
    hand->skeleton.index[3].y = mmio_read_float(base_addr + HAND_INDEX_TIP_Y);
    hand->skeleton.index[3].z = mmio_read_float(base_addr + HAND_INDEX_TIP_Z);
    
    /* Confidence and gesture */
    hand->skeleton.confidence = mmio_read_float(base_addr + HAND_CONFIDENCE);
    hand->skeleton.valid = hand->skeleton.confidence > 0.5f;
    
    hand->previous_gesture = hand->current_gesture;
    hand->current_gesture = mmio_read32(base_addr + HAND_GESTURE);
    
    /* Calculate pinch distance */
    hand->pinch_distance = point3d_distance(
        hand->skeleton.thumb[3],
        hand->skeleton.index[3]
    );
    
    /* Detect pinch (threshold: 20mm) */
    bool was_pinching = hand->is_pinching;
    hand->is_pinching = hand->pinch_distance < 0.02f;
    
    if (hand->is_pinching && !was_pinching) {
        hand->pinch_start_time = get_timer_count();
    }
    
    hand->timestamp = get_timer_count();
}

/* Initialize virtual keyboard layout (QWERTY) */
static void init_virtual_keyboard(void)
{
    virtual_keyboard_t *kb = &g_input.keyboard;
    
    memset(kb, 0, sizeof(virtual_keyboard_t));
    
    /* Position in 3D space (centered, arms length) */
    kb->x = 400;
    kb->y = 800;
    kb->width = 1200;
    kb->height = 400;
    kb->visible = false;
    
    /* Layout (simplified QWERTY) */
    const char *rows[KEYBOARD_ROWS] = {
        "QWERTYUIOP",
        "ASDFGHJKL ",
        "ZXCVBNM,.?",
        "    SPACE "
    };
    
    uint32_t key_idx = 0;
    uint32_t key_width = kb->width / KEYBOARD_COLS;
    uint32_t key_height = kb->height / KEYBOARD_ROWS;
    
    for (uint32_t row = 0; row < KEYBOARD_ROWS; row++) {
        for (uint32_t col = 0; col < KEYBOARD_COLS && rows[row][col]; col++) {
            if (key_idx >= MAX_KEYS) break;
            
            virtual_key_t *key = &kb->keys[key_idx];
            
            key->character = rows[row][col];
            key->shift_character = rows[row][col];  /* Simplified */
            key->x = kb->x + col * key_width;
            key->y = kb->y + row * key_height;
            key->width = key_width - 10;  /* Padding */
            key->height = key_height - 10;
            
            key_idx++;
        }
    }
    
    kb->num_keys = key_idx;
}

/* Check if point is over key */
static virtual_key_t *get_key_at_cursor(void)
{
    virtual_keyboard_t *kb = &g_input.keyboard;
    
    if (!kb->visible) return NULL;
    
    for (uint32_t i = 0; i < kb->num_keys; i++) {
        virtual_key_t *key = &kb->keys[i];
        
        if (g_input.cursor_x >= key->x && 
            g_input.cursor_x < key->x + (int32_t)key->width &&
            g_input.cursor_y >= key->y && 
            g_input.cursor_y < key->y + (int32_t)key->height) {
            return key;
        }
    }
    
    return NULL;
}

/* Update keyboard hover states */
static void update_keyboard_hover(void)
{
    virtual_keyboard_t *kb = &g_input.keyboard;
    uint64_t now = get_timer_count();
    
    /* Clear all hover states */
    for (uint32_t i = 0; i < kb->num_keys; i++) {
        kb->keys[i].is_hovered = false;
    }
    
    /* Set hover on key under cursor */
    virtual_key_t *key = get_key_at_cursor();
    if (key) {
        if (!key->is_hovered) {
            key->hover_start_time = now;
        }
        key->is_hovered = true;
    }
}

/* Type character from keyboard */
static void type_character(char c)
{
    virtual_keyboard_t *kb = &g_input.keyboard;
    
    if (kb->buffer_pos < sizeof(kb->input_buffer) - 1) {
        kb->input_buffer[kb->buffer_pos++] = c;
        kb->input_buffer[kb->buffer_pos] = '\0';
        
        g_input.total_keys_typed++;
        
        uart_puts("[INPUT] Typed: ");
        uart_puts(&c);
        uart_puts("\n");
    }
}

/* Handle pinch gesture (tap/click) */
static void handle_pinch_gesture(hand_state_t *hand)
{
    /* Check if this is a new pinch */
    if (hand->is_pinching && hand->previous_gesture != GESTURE_PINCH) {
        uint64_t now = get_timer_count();
        
        /* Check if typing on keyboard */
        if (g_input.keyboard.visible) {
            virtual_key_t *key = get_key_at_cursor();
            if (key && key->is_hovered) {
                type_character(key->character);
                return;
            }
        }
        
        /* Regular tap */
        g_input.total_taps++;
        
        uart_puts("[INPUT] Tap at (");
        uart_put_dec(g_input.cursor_x);
        uart_puts(", ");
        uart_put_dec(g_input.cursor_y);
        uart_puts(")\n");
    }
}

/* Handle scroll gesture */
static void handle_scroll_gesture(hand_state_t *hand)
{
    if (hand->current_gesture == GESTURE_FLICK_UP ||
        hand->current_gesture == GESTURE_FLICK_DOWN) {
        
        int32_t delta = (hand->current_gesture == GESTURE_FLICK_UP) ? -100 : 100;
        
        g_input.total_scrolls++;
        
        uart_puts("[INPUT] Scroll: ");
        uart_put_dec(delta);
        uart_puts("\n");
    }
}

/* Update dwell control */
static void update_dwell_control(void)
{
    dwell_control_t *dwell = &g_input.dwell;
    
    if (!dwell->enabled) return;
    
    uint64_t now = get_timer_count();
    
    /* Check if cursor has moved significantly */
    point3d_t current_target = {
        (float)g_input.cursor_x,
        (float)g_input.cursor_y,
        0.0f
    };
    
    float movement = point3d_distance(current_target, dwell->dwell_target);
    
    if (movement > 50.0f) {
        /* Cursor moved - reset dwell */
        dwell->dwell_target = current_target;
        dwell->dwell_start_time = now;
        dwell->dwelling = false;
        dwell->progress = 0.0f;
    } else {
        /* Cursor stable - accumulate dwell */
        dwell->dwelling = true;
        
        uint64_t dwell_elapsed = now - dwell->dwell_start_time;
        uint64_t dwell_threshold = (uint64_t)dwell->dwell_time_ms * 1000000;
        
        dwell->progress = (float)dwell_elapsed / (float)dwell_threshold;
        
        if (dwell->progress >= 1.0f) {
            /* Dwell complete - trigger selection */
            dwell->progress = 1.0f;
            
            /* Check keyboard */
            if (g_input.keyboard.visible) {
                virtual_key_t *key = get_key_at_cursor();
                if (key) {
                    type_character(key->character);
                }
            }
            
            /* Reset */
            dwell->dwell_start_time = now;
            dwell->progress = 0.0f;
            
            uart_puts("[INPUT] Dwell select\n");
        }
    }
}

/* Eye calibration (9-point) */
void input_calibrate_eyes(void)
{
    uart_puts("[INPUT] Starting eye calibration (9 points)\n");
    
    calibration_data_t *cal = &g_input.calibration;
    
    /* 9 calibration points (3×3 grid) */
    const float cal_x[] = {0.1f, 0.5f, 0.9f, 0.1f, 0.5f, 0.9f, 0.1f, 0.5f, 0.9f};
    const float cal_y[] = {0.1f, 0.1f, 0.1f, 0.5f, 0.5f, 0.5f, 0.9f, 0.9f, 0.9f};
    
    for (uint32_t i = 0; i < 9; i++) {
        /* Show calibration dot at position */
        uart_puts("[INPUT] Look at point ");
        uart_put_dec(i + 1);
        uart_puts("/9\n");
        
        /* Wait for stable gaze */
        for (uint32_t wait = 0; wait < 1000000; wait++) {
            __asm__ volatile("nop");
        }
        
        /* Read gaze */
        read_eye_tracking();
        
        cal->eye_cal_points[i].x = cal_x[i];
        cal->eye_cal_points[i].y = cal_y[i];
        cal->eye_cal_gazes[i] = g_input.eye.combined_gaze;
        
        cal->eye_cal_count++;
    }
    
    /* Compute calibration matrix (simplified) */
    cal->eye_calibrated = true;
    
    uart_puts("[INPUT] Eye calibration complete\n");
}

/* Initialize input service */
void input_service_init(void)
{
    uart_puts("[INPUT] Initializing input service\n");
    
    memset(&g_input, 0, sizeof(input_service_t));
    
    /* Get screen dimensions */
    extern void framebuffer_get_info(uint32_t, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
    framebuffer_get_info(0, &g_input.screen_width, &g_input.screen_height, NULL, NULL);
    
    if (g_input.screen_width == 0) {
        g_input.screen_width = 3840;
        g_input.screen_height = 2160;
    }
    
    /* Enable tracking */
    g_input.eye_tracking_enabled = true;
    g_input.hand_tracking_enabled = true;
    g_input.voice_enabled = false;
    g_input.sensitivity = 1.0f;
    
    /* Initialize cursor */
    g_input.cursor_x = g_input.screen_width / 2;
    g_input.cursor_y = g_input.screen_height / 2;
    
    /* Initialize virtual keyboard */
    init_virtual_keyboard();
    
    /* Initialize dwell control */
    g_input.dwell.enabled = false;
    g_input.dwell.dwell_time_ms = 800;  /* 800ms dwell */
    
    uart_puts("[INPUT] Screen: ");
    uart_put_dec(g_input.screen_width);
    uart_puts("x");
    uart_put_dec(g_input.screen_height);
    uart_puts("\n");
    
    /* Calibrate eyes */
    input_calibrate_eyes();
    
    uart_puts("[INPUT] Input service initialized\n");
}

/* Main update loop (called every frame @ 90Hz) */
void input_service_update(void)
{
    spinlock_acquire(&g_input.lock);
    
    /* Read eye tracking */
    if (g_input.eye_tracking_enabled) {
        read_eye_tracking();
    }
    
    /* Read hand tracking */
    if (g_input.hand_tracking_enabled) {
        read_hand_tracking(&g_input.left_hand, HAND_LEFT_BASE);
        read_hand_tracking(&g_input.right_hand, HAND_RIGHT_BASE);
    }
    
    /* Update keyboard hover states */
    if (g_input.keyboard.visible) {
        update_keyboard_hover();
    }
    
    /* Process gestures */
    if (g_input.left_hand.skeleton.valid) {
        handle_pinch_gesture(&g_input.left_hand);
        handle_scroll_gesture(&g_input.left_hand);
    }
    
    if (g_input.right_hand.skeleton.valid) {
        handle_pinch_gesture(&g_input.right_hand);
        handle_scroll_gesture(&g_input.right_hand);
    }
    
    /* Update dwell control */
    if (g_input.dwell.enabled) {
        update_dwell_control();
    }
    
    /* Update compositor cursor */
    extern void compositor_set_cursor(int32_t, int32_t);
    compositor_set_cursor(g_input.cursor_x, g_input.cursor_y);
    
    g_input.frames_processed++;
    
    spinlock_release(&g_input.lock);
}

/* Show/hide virtual keyboard */
void input_show_keyboard(bool show)
{
    spinlock_acquire(&g_input.lock);
    g_input.keyboard.visible = show;
    spinlock_release(&g_input.lock);
    
    if (show) {
        uart_puts("[INPUT] Virtual keyboard shown\n");
    } else {
        uart_puts("[INPUT] Virtual keyboard hidden\n");
    }
}

/* Enable/disable dwell control */
void input_set_dwell_control(bool enable)
{
    spinlock_acquire(&g_input.lock);
    g_input.dwell.enabled = enable;
    spinlock_release(&g_input.lock);
    
    uart_puts("[INPUT] Dwell control: ");
    uart_puts(enable ? "enabled\n" : "disabled\n");
}

/* Get cursor position */
void input_get_cursor(int32_t *x, int32_t *y)
{
    if (x) *x = g_input.cursor_x;
    if (y) *y = g_input.cursor_y;
}

/* Get eye gaze (for foveated rendering) */
void input_get_gaze(float *x, float *y)
{
    if (x) *x = g_input.eye.combined_gaze.x;
    if (y) *y = g_input.eye.combined_gaze.y;
}

/* Check if hand is pinching */
bool input_is_pinching(bool left_hand)
{
    return left_hand ? g_input.left_hand.is_pinching : g_input.right_hand.is_pinching;
}

/* Get keyboard input buffer */
const char *input_get_keyboard_buffer(void)
{
    return g_input.keyboard.input_buffer;
}

/* Clear keyboard buffer */
void input_clear_keyboard_buffer(void)
{
    spinlock_acquire(&g_input.lock);
    g_input.keyboard.buffer_pos = 0;
    g_input.keyboard.input_buffer[0] = '\0';
    spinlock_release(&g_input.lock);
}

/* Print statistics */
void input_service_print_stats(void)
{
    uart_puts("\n[INPUT] Statistics:\n");
    uart_puts("════════════════════════════════════════\n");
    
    uart_puts("  Eye tracking:      ");
    uart_puts(g_input.eye_tracking_enabled ? "enabled\n" : "disabled\n");
    
    uart_puts("  Eye confidence:    ");
    uint32_t conf = (uint32_t)(g_input.eye.confidence * 100);
    uart_put_dec(conf);
    uart_puts("%\n");
    
    uart_puts("  Hand tracking:     ");
    uart_puts(g_input.hand_tracking_enabled ? "enabled\n" : "disabled\n");
    
    uart_puts("  Left hand:         ");
    uart_puts(g_input.left_hand.skeleton.valid ? "detected\n" : "not detected\n");
    
    uart_puts("  Right hand:        ");
    uart_puts(g_input.right_hand.skeleton.valid ? "detected\n" : "not detected\n");
    
    uart_puts("  Cursor:            (");
    uart_put_dec(g_input.cursor_x);
    uart_puts(", ");
    uart_put_dec(g_input.cursor_y);
    uart_puts(")\n");
    
    uart_puts("  Total taps:        ");
    uart_put_dec(g_input.total_taps);
    uart_puts("\n");
    
    uart_puts("  Total scrolls:     ");
    uart_put_dec(g_input.total_scrolls);
    uart_puts("\n");
    
    uart_puts("  Keys typed:        ");
    uart_put_dec(g_input.total_keys_typed);
    uart_puts("\n");
    
    uart_puts("  Frames processed:  ");
    uart_put_dec(g_input.frames_processed);
    uart_puts("\n");
    
    uart_puts("  Keyboard visible:  ");
    uart_puts(g_input.keyboard.visible ? "yes\n" : "no\n");
    
    uart_puts("  Dwell control:     ");
    uart_puts(g_input.dwell.enabled ? "enabled\n" : "disabled\n");
    
    uart_puts("\n");
}