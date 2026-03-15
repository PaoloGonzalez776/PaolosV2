/*
 * event_bus.c - Event Bus (Publish/Subscribe)
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Sistema central de mensajería pub/sub desacoplado.
 * Permite comunicación asíncrona entre componentes sin dependencias directas.
 * 
 * CARACTERÍSTICAS:
 * - Topic-based pub/sub
 * - Multiple subscribers per topic
 * - Event filtering
 * - Priority events
 * - Sticky events (last event cached)
 * - Event history/replay
 * - Wildcard subscriptions
 * - Async delivery
 * 
 * TOPICS WELL-KNOWN:
 * - "system.mode_change"      - System mode changed
 * - "system.low_battery"       - Low battery warning
 * - "system.thermal_warning"   - Thermal warning
 * - "window.created"           - Window created
 * - "window.destroyed"         - Window destroyed
 * - "window.focused"           - Window got focus
 * - "input.gesture"            - Gesture detected
 * - "sensor.orientation"       - Orientation changed
 * - "app.launched"             - App launched
 * - "app.terminated"           - App terminated
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

/* Event priority */
typedef enum {
    EVENT_BUS_PRIORITY_LOW      = 0,
    EVENT_BUS_PRIORITY_NORMAL   = 1,
    EVENT_BUS_PRIORITY_HIGH     = 2,
    EVENT_BUS_PRIORITY_CRITICAL = 3,
} event_bus_priority_t;

/* Event flags */
#define EVENT_FLAG_STICKY       0x0001  /* Cache last event */
#define EVENT_FLAG_NO_HISTORY   0x0002  /* Don't store in history */
#define EVENT_FLAG_BROADCAST    0x0004  /* Broadcast to all */

/* Maximum sizes */
#define MAX_TOPIC_LEN           128
#define MAX_EVENT_PAYLOAD       2048
#define MAX_SUBSCRIBERS         1000
#define MAX_HISTORY_PER_TOPIC   10

/* Event data */
typedef struct {
    char topic[MAX_TOPIC_LEN];
    event_bus_priority_t priority;
    uint32_t flags;
    
    uint32_t publisher_pid;
    uint64_t timestamp;
    uint64_t event_id;
    
    uint32_t payload_size;
    uint8_t payload[MAX_EVENT_PAYLOAD];
    
} event_bus_event_t;

/* Subscriber callback */
typedef void (*event_bus_callback_t)(const event_bus_event_t *event, void *user_data);

/* Subscriber */
typedef struct subscriber {
    uint32_t subscriber_id;
    uint32_t pid;
    
    char topic_filter[MAX_TOPIC_LEN];  /* Can include wildcards */
    
    event_bus_callback_t callback;
    void *user_data;
    
    bool enabled;
    uint32_t events_received;
    uint64_t last_event_time;
    
    struct subscriber *next;
    
} subscriber_t;

/* Sticky event (last event cached per topic) */
typedef struct sticky_event {
    char topic[MAX_TOPIC_LEN];
    event_bus_event_t event;
    
    struct sticky_event *next;
    
} sticky_event_t;

/* Event history node */
typedef struct event_history_node {
    event_bus_event_t event;
    struct event_history_node *next;
} event_history_node_t;

/* Topic history */
typedef struct topic_history {
    char topic[MAX_TOPIC_LEN];
    
    event_history_node_t *events;
    uint32_t count;
    
    struct topic_history *next;
    
} topic_history_t;

/* Event bus state */
typedef struct {
    /* Subscribers */
    subscriber_t *subscribers;
    uint32_t num_subscribers;
    uint32_t next_subscriber_id;
    
    /* Sticky events */
    sticky_event_t *sticky_events;
    uint32_t num_sticky;
    
    /* Event history */
    topic_history_t *history;
    
    /* Configuration */
    visor_mode_t current_mode;
    bool history_enabled;
    uint32_t max_history_per_topic;
    
    /* Statistics */
    uint64_t total_events_published;
    uint64_t total_events_delivered;
    uint64_t next_event_id;
    
    uint64_t events_per_priority[4];
    
    volatile uint32_t lock;
    
} event_bus_t;

/* Global state */
static event_bus_t g_bus;

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

static int str_cmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static char *str_cpy(char *dst, const char *src)
{
    char *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

/* Topic matching with wildcards */
static bool topic_matches(const char *filter, const char *topic)
{
    /* Simple wildcard matching:
     * "*" matches everything
     * "prefix.*" matches "prefix.anything"
     * "*.suffix" matches "anything.suffix"
     */
    
    if (str_cmp(filter, "*") == 0) {
        return true;
    }
    
    size_t filter_len = str_len(filter);
    size_t topic_len = str_len(topic);
    
    /* Check for "prefix.*" pattern */
    if (filter_len > 2 && filter[filter_len-2] == '.' && filter[filter_len-1] == '*') {
        size_t prefix_len = filter_len - 2;
        
        if (topic_len >= prefix_len) {
            for (size_t i = 0; i < prefix_len; i++) {
                if (filter[i] != topic[i]) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }
    
    /* Check for "*.suffix" pattern */
    if (filter_len > 2 && filter[0] == '*' && filter[1] == '.') {
        const char *suffix = filter + 2;
        size_t suffix_len = filter_len - 2;
        
        if (topic_len >= suffix_len) {
            const char *topic_suffix = topic + (topic_len - suffix_len);
            return str_cmp(suffix, topic_suffix) == 0;
        }
        return false;
    }
    
    /* Exact match */
    return str_cmp(filter, topic) == 0;
}

/* Initialize event bus */
void event_bus_init(void)
{
    uart_puts("[EVENTBUS] Initializing event bus\n");
    
    memset(&g_bus, 0, sizeof(event_bus_t));
    
    /* Get current mode */
    extern visor_mode_t mode_get_current(void);
    g_bus.current_mode = mode_get_current();
    
    /* Configuration based on mode */
    switch (g_bus.current_mode) {
        case 0:  /* Phone */
            g_bus.history_enabled = false;
            g_bus.max_history_per_topic = 5;
            break;
            
        case 1:  /* Tablet */
            g_bus.history_enabled = true;
            g_bus.max_history_per_topic = 10;
            break;
            
        case 2:  /* Laptop */
            g_bus.history_enabled = true;
            g_bus.max_history_per_topic = 20;
            break;
            
        case 3:  /* TV */
            g_bus.history_enabled = false;
            g_bus.max_history_per_topic = 3;
            break;
    }
    
    g_bus.next_subscriber_id = 1;
    g_bus.next_event_id = 1;
    
    uart_puts("[EVENTBUS] Mode: ");
    const char *mode_names[] = {"Phone", "Tablet", "Laptop", "TV"};
    uart_puts(mode_names[g_bus.current_mode]);
    uart_puts("\n");
    
    uart_puts("[EVENTBUS] Event bus initialized\n");
}

/* Subscribe to topic */
uint32_t event_bus_subscribe(uint32_t pid, const char *topic_filter, 
                            event_bus_callback_t callback, void *user_data)
{
    if (!topic_filter || !callback) {
        return 0;
    }
    
    if (str_len(topic_filter) >= MAX_TOPIC_LEN) {
        return 0;
    }
    
    spinlock_acquire(&g_bus.lock);
    
    /* Allocate subscriber */
    subscriber_t *sub = (subscriber_t *)kalloc(sizeof(subscriber_t));
    if (!sub) {
        spinlock_release(&g_bus.lock);
        return 0;
    }
    
    memset(sub, 0, sizeof(subscriber_t));
    
    sub->subscriber_id = g_bus.next_subscriber_id++;
    sub->pid = pid;
    str_cpy(sub->topic_filter, topic_filter);
    sub->callback = callback;
    sub->user_data = user_data;
    sub->enabled = true;
    
    /* Add to list */
    sub->next = g_bus.subscribers;
    g_bus.subscribers = sub;
    g_bus.num_subscribers++;
    
    uint32_t id = sub->subscriber_id;
    
    spinlock_release(&g_bus.lock);
    
    /* Deliver sticky events if any match */
    sticky_event_t *sticky = g_bus.sticky_events;
    while (sticky) {
        if (topic_matches(topic_filter, sticky->topic)) {
            callback(&sticky->event, user_data);
        }
        sticky = sticky->next;
    }
    
    uart_puts("[EVENTBUS] Subscribed: ");
    uart_puts(topic_filter);
    uart_puts(" (ID ");
    uart_put_dec(id);
    uart_puts(")\n");
    
    return id;
}

/* Unsubscribe */
void event_bus_unsubscribe(uint32_t subscriber_id)
{
    spinlock_acquire(&g_bus.lock);
    
    subscriber_t *sub = g_bus.subscribers;
    subscriber_t *prev = NULL;
    
    while (sub) {
        if (sub->subscriber_id == subscriber_id) {
            if (prev) {
                prev->next = sub->next;
            } else {
                g_bus.subscribers = sub->next;
            }
            
            kfree(sub);
            g_bus.num_subscribers--;
            break;
        }
        
        prev = sub;
        sub = sub->next;
    }
    
    spinlock_release(&g_bus.lock);
}

/* Add to topic history */
static void add_to_history(const event_bus_event_t *event)
{
    if (!g_bus.history_enabled || (event->flags & EVENT_FLAG_NO_HISTORY)) {
        return;
    }
    
    /* Find or create topic history */
    topic_history_t *hist = g_bus.history;
    while (hist) {
        if (str_cmp(hist->topic, event->topic) == 0) {
            break;
        }
        hist = hist->next;
    }
    
    if (!hist) {
        /* Create new topic history */
        hist = (topic_history_t *)kalloc(sizeof(topic_history_t));
        if (!hist) return;
        
        memset(hist, 0, sizeof(topic_history_t));
        str_cpy(hist->topic, event->topic);
        
        hist->next = g_bus.history;
        g_bus.history = hist;
    }
    
    /* Add event to history */
    event_history_node_t *node = (event_history_node_t *)kalloc(sizeof(event_history_node_t));
    if (!node) return;
    
    memcpy(&node->event, event, sizeof(event_bus_event_t));
    node->next = hist->events;
    hist->events = node;
    hist->count++;
    
    /* Limit history size */
    if (hist->count > g_bus.max_history_per_topic) {
        event_history_node_t *curr = hist->events;
        event_history_node_t *prev = NULL;
        uint32_t count = 0;
        
        while (curr && count < g_bus.max_history_per_topic - 1) {
            prev = curr;
            curr = curr->next;
            count++;
        }
        
        if (prev) {
            prev->next = NULL;
        }
        
        while (curr) {
            event_history_node_t *next = curr->next;
            kfree(curr);
            curr = next;
            hist->count--;
        }
    }
}

/* Update sticky event */
static void update_sticky(const event_bus_event_t *event)
{
    /* Find existing sticky event */
    sticky_event_t *sticky = g_bus.sticky_events;
    sticky_event_t *prev = NULL;
    
    while (sticky) {
        if (str_cmp(sticky->topic, event->topic) == 0) {
            /* Update existing */
            memcpy(&sticky->event, event, sizeof(event_bus_event_t));
            return;
        }
        prev = sticky;
        sticky = sticky->next;
    }
    
    /* Create new sticky event */
    sticky = (sticky_event_t *)kalloc(sizeof(sticky_event_t));
    if (!sticky) return;
    
    str_cpy(sticky->topic, event->topic);
    memcpy(&sticky->event, event, sizeof(event_bus_event_t));
    
    sticky->next = g_bus.sticky_events;
    g_bus.sticky_events = sticky;
    g_bus.num_sticky++;
}

/* Publish event */
void event_bus_publish(uint32_t publisher_pid, const char *topic,
                      event_bus_priority_t priority, uint32_t flags,
                      const void *payload, uint32_t payload_size)
{
    if (!topic || str_len(topic) >= MAX_TOPIC_LEN) {
        return;
    }
    
    if (payload_size > MAX_EVENT_PAYLOAD) {
        return;
    }
    
    spinlock_acquire(&g_bus.lock);
    
    /* Build event */
    event_bus_event_t event;
    memset(&event, 0, sizeof(event_bus_event_t));
    
    str_cpy(event.topic, topic);
    event.priority = priority;
    event.flags = flags;
    event.publisher_pid = publisher_pid;
    event.timestamp = get_timer_count();
    event.event_id = g_bus.next_event_id++;
    event.payload_size = payload_size;
    
    if (payload && payload_size > 0) {
        memcpy(event.payload, payload, payload_size);
    }
    
    /* Update statistics */
    g_bus.total_events_published++;
    if (priority < 4) {
        g_bus.events_per_priority[priority]++;
    }
    
    /* Handle sticky events */
    if (flags & EVENT_FLAG_STICKY) {
        update_sticky(&event);
    }
    
    /* Add to history */
    add_to_history(&event);
    
    /* Deliver to subscribers */
    subscriber_t *sub = g_bus.subscribers;
    uint32_t delivered = 0;
    
    while (sub) {
        if (sub->enabled && topic_matches(sub->topic_filter, topic)) {
            /* Deliver event */
            sub->callback(&event, sub->user_data);
            sub->events_received++;
            sub->last_event_time = event.timestamp;
            delivered++;
        }
        sub = sub->next;
    }
    
    g_bus.total_events_delivered += delivered;
    
    spinlock_release(&g_bus.lock);
}

/* Publish simple (convenience) */
void event_bus_publish_simple(uint32_t pid, const char *topic)
{
    event_bus_publish(pid, topic, EVENT_BUS_PRIORITY_NORMAL, 0, NULL, 0);
}

/* Get topic history */
uint32_t event_bus_get_history(const char *topic, event_bus_event_t *events_out, 
                               uint32_t max_events)
{
    if (!topic || !events_out || max_events == 0) {
        return 0;
    }
    
    spinlock_acquire(&g_bus.lock);
    
    /* Find topic history */
    topic_history_t *hist = g_bus.history;
    while (hist) {
        if (str_cmp(hist->topic, topic) == 0) {
            break;
        }
        hist = hist->next;
    }
    
    if (!hist) {
        spinlock_release(&g_bus.lock);
        return 0;
    }
    
    /* Copy events */
    uint32_t count = 0;
    event_history_node_t *node = hist->events;
    
    while (node && count < max_events) {
        memcpy(&events_out[count], &node->event, sizeof(event_bus_event_t));
        count++;
        node = node->next;
    }
    
    spinlock_release(&g_bus.lock);
    return count;
}

/* Enable/disable subscriber */
void event_bus_set_subscriber_enabled(uint32_t subscriber_id, bool enabled)
{
    spinlock_acquire(&g_bus.lock);
    
    subscriber_t *sub = g_bus.subscribers;
    while (sub) {
        if (sub->subscriber_id == subscriber_id) {
            sub->enabled = enabled;
            break;
        }
        sub = sub->next;
    }
    
    spinlock_release(&g_bus.lock);
}

/* Clear topic history */
void event_bus_clear_history(const char *topic)
{
    spinlock_acquire(&g_bus.lock);
    
    topic_history_t *hist = g_bus.history;
    topic_history_t *prev = NULL;
    
    while (hist) {
        if (str_cmp(hist->topic, topic) == 0) {
            /* Free all events */
            event_history_node_t *node = hist->events;
            while (node) {
                event_history_node_t *next = node->next;
                kfree(node);
                node = next;
            }
            
            /* Remove history */
            if (prev) {
                prev->next = hist->next;
            } else {
                g_bus.history = hist->next;
            }
            
            kfree(hist);
            break;
        }
        
        prev = hist;
        hist = hist->next;
    }
    
    spinlock_release(&g_bus.lock);
}

/* Reconfigure for mode change */
void event_bus_reconfigure(visor_mode_t mode)
{
    spinlock_acquire(&g_bus.lock);
    
    g_bus.current_mode = mode;
    
    switch (mode) {
        case 0:  /* Phone */
            g_bus.history_enabled = false;
            g_bus.max_history_per_topic = 5;
            break;
        case 1:  /* Tablet */
            g_bus.history_enabled = true;
            g_bus.max_history_per_topic = 10;
            break;
        case 2:  /* Laptop */
            g_bus.history_enabled = true;
            g_bus.max_history_per_topic = 20;
            break;
        case 3:  /* TV */
            g_bus.history_enabled = false;
            g_bus.max_history_per_topic = 3;
            break;
    }
    
    spinlock_release(&g_bus.lock);
}

/* Print statistics */
void event_bus_print_stats(void)
{
    uart_puts("\n[EVENTBUS] Statistics:\n");
    uart_puts("════════════════════════════════════════\n");
    
    uart_puts("  Mode:              ");
    const char *mode_names[] = {"Phone", "Tablet", "Laptop", "TV"};
    uart_puts(mode_names[g_bus.current_mode]);
    uart_puts("\n");
    
    uart_puts("  Subscribers:       ");
    uart_put_dec(g_bus.num_subscribers);
    uart_puts("\n");
    
    uart_puts("  Events published:  ");
    uart_put_dec(g_bus.total_events_published);
    uart_puts("\n");
    
    uart_puts("  Events delivered:  ");
    uart_put_dec(g_bus.total_events_delivered);
    uart_puts("\n");
    
    uart_puts("  Sticky events:     ");
    uart_put_dec(g_bus.num_sticky);
    uart_puts("\n");
    
    uart_puts("  History enabled:   ");
    uart_puts(g_bus.history_enabled ? "yes\n" : "no\n");
    
    uart_puts("  Events by priority:\n");
    uart_puts("    Critical:        ");
    uart_put_dec(g_bus.events_per_priority[EVENT_BUS_PRIORITY_CRITICAL]);
    uart_puts("\n");
    uart_puts("    High:            ");
    uart_put_dec(g_bus.events_per_priority[EVENT_BUS_PRIORITY_HIGH]);
    uart_puts("\n");
    uart_puts("    Normal:          ");
    uart_put_dec(g_bus.events_per_priority[EVENT_BUS_PRIORITY_NORMAL]);
    uart_puts("\n");
    uart_puts("    Low:             ");
    uart_put_dec(g_bus.events_per_priority[EVENT_BUS_PRIORITY_LOW]);
    uart_puts("\n");
    
    uart_puts("\n");
}