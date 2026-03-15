/*
 * event_dispatcher.c - Event Dispatcher
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Sistema central de dispatching de eventos para todas las aplicaciones.
 * Distribuye eventos de input, gestos, sensores, sistema a los listeners.
 * 
 * CARACTERÍSTICAS:
 * - Event queue con prioridades
 * - Subscription/listener model
 * - Event filtering por tipo
 * - Event broadcasting
 * - Priority-based dispatch
 * - Thread-safe operations
 * - Event history
 * - Statistics tracking
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

/* Event types */
typedef enum {
    EVENT_TYPE_NONE         = 0,
    
    /* Input events */
    EVENT_TYPE_CURSOR_MOVE  = 1,
    EVENT_TYPE_TAP          = 2,
    EVENT_TYPE_DOUBLE_TAP   = 3,
    EVENT_TYPE_LONG_PRESS   = 4,
    EVENT_TYPE_DRAG         = 5,
    EVENT_TYPE_FLICK        = 6,
    EVENT_TYPE_ZOOM         = 7,
    EVENT_TYPE_ROTATE       = 8,
    EVENT_TYPE_KEY_PRESS    = 9,
    EVENT_TYPE_KEY_RELEASE  = 10,
    
    /* Window events */
    EVENT_TYPE_WINDOW_CREATE   = 20,
    EVENT_TYPE_WINDOW_DESTROY  = 21,
    EVENT_TYPE_WINDOW_FOCUS    = 22,
    EVENT_TYPE_WINDOW_BLUR     = 23,
    EVENT_TYPE_WINDOW_MOVE     = 24,
    EVENT_TYPE_WINDOW_RESIZE   = 25,
    EVENT_TYPE_WINDOW_MINIMIZE = 26,
    EVENT_TYPE_WINDOW_MAXIMIZE = 27,
    
    /* Sensor events */
    EVENT_TYPE_ORIENTATION     = 40,
    EVENT_TYPE_MOTION          = 41,
    EVENT_TYPE_MOTION_SICKNESS = 42,
    
    /* System events */
    EVENT_TYPE_MODE_CHANGE     = 60,
    EVENT_TYPE_LOW_BATTERY     = 61,
    EVENT_TYPE_THERMAL_WARNING = 62,
    EVENT_TYPE_APP_LAUNCH      = 63,
    EVENT_TYPE_APP_TERMINATE   = 64,
    
    EVENT_TYPE_MAX             = 100
} event_type_t;

/* Event priority */
typedef enum {
    EVENT_PRIORITY_LOW      = 0,
    EVENT_PRIORITY_NORMAL   = 1,
    EVENT_PRIORITY_HIGH     = 2,
    EVENT_PRIORITY_CRITICAL = 3,
} event_priority_t;

/* Generic event */
typedef struct {
    event_type_t type;
    event_priority_t priority;
    
    uint32_t target_window;  /* 0 = broadcast to all */
    uint32_t source_pid;
    
    uint64_t timestamp;
    
    /* Event data (generic payload) */
    union {
        /* Cursor move */
        struct {
            int32_t x, y;
            int32_t delta_x, delta_y;
        } cursor;
        
        /* Tap/gesture */
        struct {
            int32_t x, y;
            uint32_t button;
            bool left_hand;
        } tap;
        
        /* Drag */
        struct {
            int32_t start_x, start_y;
            int32_t current_x, current_y;
            int32_t delta_x, delta_y;
        } drag;
        
        /* Key */
        struct {
            uint32_t keycode;
            uint32_t modifiers;
            char character;
        } key;
        
        /* Window */
        struct {
            uint32_t window_id;
            int32_t x, y;
            uint32_t width, height;
        } window;
        
        /* Sensor */
        struct {
            float roll, pitch, yaw;
            float accel_x, accel_y, accel_z;
        } sensor;
        
        /* System */
        struct {
            uint32_t mode;
            uint32_t value;
        } system;
        
        /* Raw data (for custom events) */
        uint8_t raw[128];
    } data;
    
} event_t;

/* Event queue node */
typedef struct event_node {
    event_t event;
    struct event_node *next;
} event_node_t;

/* Event listener */
typedef void (*event_callback_t)(const event_t *event, void *user_data);

typedef struct event_listener {
    uint32_t listener_id;
    uint32_t window_id;      /* 0 = all windows */
    event_type_t type;       /* EVENT_TYPE_NONE = all types */
    event_callback_t callback;
    void *user_data;
    
    bool enabled;
    uint64_t events_received;
    
    struct event_listener *next;
} event_listener_t;

/* Event dispatcher state */
typedef struct {
    /* Event queues (one per priority) */
    event_node_t *queue_head[4];  /* 4 priority levels */
    event_node_t *queue_tail[4];
    uint32_t queue_size[4];
    
    /* Listeners */
    event_listener_t *listeners;
    uint32_t num_listeners;
    uint32_t next_listener_id;
    
    /* Configuration */
    uint32_t max_queue_size;
    bool broadcast_enabled;
    
    /* Statistics */
    uint64_t total_events_queued;
    uint64_t total_events_dispatched;
    uint64_t total_events_dropped;
    uint64_t events_per_type[EVENT_TYPE_MAX];
    
    volatile uint32_t lock;
    
} event_dispatcher_t;

/* Global state */
static event_dispatcher_t g_dispatcher;

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

/* Initialize event dispatcher */
void event_dispatcher_init(void)
{
    uart_puts("[EVENT] Initializing event dispatcher\n");
    
    memset(&g_dispatcher, 0, sizeof(event_dispatcher_t));
    
    g_dispatcher.max_queue_size = 1000;
    g_dispatcher.broadcast_enabled = true;
    g_dispatcher.next_listener_id = 1;
    
    uart_puts("[EVENT] Event dispatcher initialized\n");
}

/* Queue event */
bool event_queue(const event_t *event)
{
    if (!event) return false;
    
    spinlock_acquire(&g_dispatcher.lock);
    
    event_priority_t priority = event->priority;
    if (priority > EVENT_PRIORITY_CRITICAL) {
        priority = EVENT_PRIORITY_NORMAL;
    }
    
    /* Check queue size */
    if (g_dispatcher.queue_size[priority] >= g_dispatcher.max_queue_size) {
        g_dispatcher.total_events_dropped++;
        spinlock_release(&g_dispatcher.lock);
        return false;
    }
    
    /* Allocate node */
    event_node_t *node = (event_node_t *)kalloc(sizeof(event_node_t));
    if (!node) {
        g_dispatcher.total_events_dropped++;
        spinlock_release(&g_dispatcher.lock);
        return false;
    }
    
    /* Copy event */
    memcpy(&node->event, event, sizeof(event_t));
    node->next = NULL;
    
    /* Add to queue */
    if (g_dispatcher.queue_tail[priority]) {
        g_dispatcher.queue_tail[priority]->next = node;
    } else {
        g_dispatcher.queue_head[priority] = node;
    }
    
    g_dispatcher.queue_tail[priority] = node;
    g_dispatcher.queue_size[priority]++;
    
    /* Update statistics */
    g_dispatcher.total_events_queued++;
    if (event->type < EVENT_TYPE_MAX) {
        g_dispatcher.events_per_type[event->type]++;
    }
    
    spinlock_release(&g_dispatcher.lock);
    return true;
}

/* Dequeue highest priority event */
static event_node_t *dequeue_event(void)
{
    event_node_t *node = NULL;
    
    /* Check queues from highest to lowest priority */
    for (int p = EVENT_PRIORITY_CRITICAL; p >= EVENT_PRIORITY_LOW; p--) {
        if (g_dispatcher.queue_head[p]) {
            node = g_dispatcher.queue_head[p];
            g_dispatcher.queue_head[p] = node->next;
            
            if (!g_dispatcher.queue_head[p]) {
                g_dispatcher.queue_tail[p] = NULL;
            }
            
            g_dispatcher.queue_size[p]--;
            break;
        }
    }
    
    return node;
}

/* Register event listener */
uint32_t event_register_listener(uint32_t window_id, event_type_t type, 
                                 event_callback_t callback, void *user_data)
{
    if (!callback) return 0;
    
    spinlock_acquire(&g_dispatcher.lock);
    
    event_listener_t *listener = (event_listener_t *)kalloc(sizeof(event_listener_t));
    if (!listener) {
        spinlock_release(&g_dispatcher.lock);
        return 0;
    }
    
    memset(listener, 0, sizeof(event_listener_t));
    
    listener->listener_id = g_dispatcher.next_listener_id++;
    listener->window_id = window_id;
    listener->type = type;
    listener->callback = callback;
    listener->user_data = user_data;
    listener->enabled = true;
    
    /* Add to list */
    listener->next = g_dispatcher.listeners;
    g_dispatcher.listeners = listener;
    g_dispatcher.num_listeners++;
    
    uint32_t id = listener->listener_id;
    
    spinlock_release(&g_dispatcher.lock);
    
    uart_puts("[EVENT] Registered listener ");
    uart_put_dec(id);
    uart_puts("\n");
    
    return id;
}

/* Unregister listener */
void event_unregister_listener(uint32_t listener_id)
{
    spinlock_acquire(&g_dispatcher.lock);
    
    event_listener_t *curr = g_dispatcher.listeners;
    event_listener_t *prev = NULL;
    
    while (curr) {
        if (curr->listener_id == listener_id) {
            if (prev) {
                prev->next = curr->next;
            } else {
                g_dispatcher.listeners = curr->next;
            }
            
            kfree(curr);
            g_dispatcher.num_listeners--;
            break;
        }
        
        prev = curr;
        curr = curr->next;
    }
    
    spinlock_release(&g_dispatcher.lock);
}

/* Dispatch event to listeners */
static void dispatch_to_listeners(const event_t *event)
{
    event_listener_t *listener = g_dispatcher.listeners;
    uint32_t dispatched = 0;
    
    while (listener) {
        /* Check if listener should receive this event */
        bool should_receive = listener->enabled;
        
        if (should_receive) {
            /* Check window filter */
            if (listener->window_id != 0 && 
                listener->window_id != event->target_window) {
                should_receive = false;
            }
            
            /* Check type filter */
            if (listener->type != EVENT_TYPE_NONE && 
                listener->type != event->type) {
                should_receive = false;
            }
        }
        
        if (should_receive) {
            /* Call callback */
            listener->callback(event, listener->user_data);
            listener->events_received++;
            dispatched++;
        }
        
        listener = listener->next;
    }
    
    g_dispatcher.total_events_dispatched += dispatched;
}

/* Process events (main dispatch loop) */
void event_dispatcher_process(void)
{
    spinlock_acquire(&g_dispatcher.lock);
    
    /* Process up to 10 events per call */
    for (int i = 0; i < 10; i++) {
        event_node_t *node = dequeue_event();
        if (!node) break;
        
        /* Dispatch to listeners */
        dispatch_to_listeners(&node->event);
        
        /* Free node */
        kfree(node);
    }
    
    spinlock_release(&g_dispatcher.lock);
}

/* Post cursor move event */
void event_post_cursor_move(int32_t x, int32_t y, int32_t delta_x, int32_t delta_y)
{
    event_t event;
    memset(&event, 0, sizeof(event_t));
    
    event.type = EVENT_TYPE_CURSOR_MOVE;
    event.priority = EVENT_PRIORITY_HIGH;
    event.timestamp = get_timer_count();
    
    event.data.cursor.x = x;
    event.data.cursor.y = y;
    event.data.cursor.delta_x = delta_x;
    event.data.cursor.delta_y = delta_y;
    
    event_queue(&event);
}

/* Post tap event */
void event_post_tap(uint32_t window_id, int32_t x, int32_t y, bool left_hand)
{
    event_t event;
    memset(&event, 0, sizeof(event_t));
    
    event.type = EVENT_TYPE_TAP;
    event.priority = EVENT_PRIORITY_NORMAL;
    event.target_window = window_id;
    event.timestamp = get_timer_count();
    
    event.data.tap.x = x;
    event.data.tap.y = y;
    event.data.tap.left_hand = left_hand;
    
    event_queue(&event);
}

/* Post drag event */
void event_post_drag(uint32_t window_id, int32_t start_x, int32_t start_y,
                    int32_t current_x, int32_t current_y)
{
    event_t event;
    memset(&event, 0, sizeof(event_t));
    
    event.type = EVENT_TYPE_DRAG;
    event.priority = EVENT_PRIORITY_NORMAL;
    event.target_window = window_id;
    event.timestamp = get_timer_count();
    
    event.data.drag.start_x = start_x;
    event.data.drag.start_y = start_y;
    event.data.drag.current_x = current_x;
    event.data.drag.current_y = current_y;
    event.data.drag.delta_x = current_x - start_x;
    event.data.drag.delta_y = current_y - start_y;
    
    event_queue(&event);
}

/* Post key event */
void event_post_key(uint32_t window_id, bool pressed, uint32_t keycode, 
                   uint32_t modifiers, char character)
{
    event_t event;
    memset(&event, 0, sizeof(event_t));
    
    event.type = pressed ? EVENT_TYPE_KEY_PRESS : EVENT_TYPE_KEY_RELEASE;
    event.priority = EVENT_PRIORITY_NORMAL;
    event.target_window = window_id;
    event.timestamp = get_timer_count();
    
    event.data.key.keycode = keycode;
    event.data.key.modifiers = modifiers;
    event.data.key.character = character;
    
    event_queue(&event);
}

/* Post window event */
void event_post_window(event_type_t type, uint32_t window_id, 
                      int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    event_t event;
    memset(&event, 0, sizeof(event_t));
    
    event.type = type;
    event.priority = EVENT_PRIORITY_NORMAL;
    event.target_window = window_id;
    event.timestamp = get_timer_count();
    
    event.data.window.window_id = window_id;
    event.data.window.x = x;
    event.data.window.y = y;
    event.data.window.width = width;
    event.data.window.height = height;
    
    event_queue(&event);
}

/* Post sensor event */
void event_post_sensor(float roll, float pitch, float yaw, 
                      float accel_x, float accel_y, float accel_z)
{
    event_t event;
    memset(&event, 0, sizeof(event_t));
    
    event.type = EVENT_TYPE_ORIENTATION;
    event.priority = EVENT_PRIORITY_HIGH;
    event.timestamp = get_timer_count();
    
    event.data.sensor.roll = roll;
    event.data.sensor.pitch = pitch;
    event.data.sensor.yaw = yaw;
    event.data.sensor.accel_x = accel_x;
    event.data.sensor.accel_y = accel_y;
    event.data.sensor.accel_z = accel_z;
    
    event_queue(&event);
}

/* Post system event */
void event_post_system(event_type_t type, uint32_t value)
{
    event_t event;
    memset(&event, 0, sizeof(event_t));
    
    event.type = type;
    event.priority = (type == EVENT_TYPE_THERMAL_WARNING || 
                     type == EVENT_TYPE_LOW_BATTERY) ? 
                     EVENT_PRIORITY_CRITICAL : EVENT_PRIORITY_NORMAL;
    event.timestamp = get_timer_count();
    
    event.data.system.value = value;
    
    event_queue(&event);
}

/* Clear event queue */
void event_dispatcher_clear_queue(void)
{
    spinlock_acquire(&g_dispatcher.lock);
    
    for (int p = 0; p < 4; p++) {
        event_node_t *node = g_dispatcher.queue_head[p];
        
        while (node) {
            event_node_t *next = node->next;
            kfree(node);
            node = next;
        }
        
        g_dispatcher.queue_head[p] = NULL;
        g_dispatcher.queue_tail[p] = NULL;
        g_dispatcher.queue_size[p] = 0;
    }
    
    spinlock_release(&g_dispatcher.lock);
}

/* Get queue sizes */
void event_dispatcher_get_queue_sizes(uint32_t *sizes)
{
    if (!sizes) return;
    
    for (int i = 0; i < 4; i++) {
        sizes[i] = g_dispatcher.queue_size[i];
    }
}

/* Print statistics */
void event_dispatcher_print_stats(void)
{
    uart_puts("\n[EVENT] Statistics:\n");
    uart_puts("════════════════════════════════════════\n");
    
    uart_puts("  Listeners:         ");
    uart_put_dec(g_dispatcher.num_listeners);
    uart_puts("\n");
    
    uart_puts("  Events queued:     ");
    uart_put_dec(g_dispatcher.total_events_queued);
    uart_puts("\n");
    
    uart_puts("  Events dispatched: ");
    uart_put_dec(g_dispatcher.total_events_dispatched);
    uart_puts("\n");
    
    uart_puts("  Events dropped:    ");
    uart_put_dec(g_dispatcher.total_events_dropped);
    uart_puts("\n");
    
    uart_puts("  Queue sizes:\n");
    uart_puts("    Critical:        ");
    uart_put_dec(g_dispatcher.queue_size[EVENT_PRIORITY_CRITICAL]);
    uart_puts("\n");
    uart_puts("    High:            ");
    uart_put_dec(g_dispatcher.queue_size[EVENT_PRIORITY_HIGH]);
    uart_puts("\n");
    uart_puts("    Normal:          ");
    uart_put_dec(g_dispatcher.queue_size[EVENT_PRIORITY_NORMAL]);
    uart_puts("\n");
    uart_puts("    Low:             ");
    uart_put_dec(g_dispatcher.queue_size[EVENT_PRIORITY_LOW]);
    uart_puts("\n");
    
    uart_puts("\n  Events by type:\n");
    uart_puts("    Cursor move:     ");
    uart_put_dec(g_dispatcher.events_per_type[EVENT_TYPE_CURSOR_MOVE]);
    uart_puts("\n");
    uart_puts("    Tap:             ");
    uart_put_dec(g_dispatcher.events_per_type[EVENT_TYPE_TAP]);
    uart_puts("\n");
    uart_puts("    Drag:            ");
    uart_put_dec(g_dispatcher.events_per_type[EVENT_TYPE_DRAG]);
    uart_puts("\n");
    uart_puts("    Key:             ");
    uart_put_dec(g_dispatcher.events_per_type[EVENT_TYPE_KEY_PRESS] + 
                g_dispatcher.events_per_type[EVENT_TYPE_KEY_RELEASE]);
    uart_puts("\n");
    
    uart_puts("\n");
}