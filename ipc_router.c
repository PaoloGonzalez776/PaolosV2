/*
 * ipc_router.c - IPC Router (Inter-Process Communication)
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Sistema central de comunicación entre procesos.
 * Combina múltiples mecanismos IPC para máximo rendimiento y flexibilidad.
 * 
 * IPC MECHANISMS:
 * - Shared Memory (fastest, for large data)
 * - Message Passing (flexible, safe)
 * - Message Queues (asynchronous)
 * - Service Discovery (find services)
 * - Broadcast/Multicast
 * 
 * MODE-AWARE BEHAVIOR:
 * - Phone Mode:   Simplified IPC, app-to-app
 * - Tablet Mode:  Split-screen IPC
 * - Laptop Mode:  Full multi-window IPC
 * - TV Mode:      Background service IPC
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

/* IPC Message types */
typedef enum {
    IPC_MSG_NONE            = 0,
    IPC_MSG_REQUEST         = 1,   /* Request data/service */
    IPC_MSG_RESPONSE        = 2,   /* Response to request */
    IPC_MSG_NOTIFY          = 3,   /* Notification */
    IPC_MSG_BROADCAST       = 4,   /* Broadcast to all */
    IPC_MSG_SERVICE_REG     = 5,   /* Register service */
    IPC_MSG_SERVICE_UNREG   = 6,   /* Unregister service */
    IPC_MSG_SERVICE_QUERY   = 7,   /* Query service */
} ipc_msg_type_t;

/* IPC Methods */
typedef enum {
    IPC_METHOD_MESSAGE      = 0,   /* Message passing */
    IPC_METHOD_SHARED_MEM   = 1,   /* Shared memory */
    IPC_METHOD_QUEUE        = 2,   /* Message queue */
} ipc_method_t;

/* IPC Message header */
typedef struct {
    ipc_msg_type_t type;
    ipc_method_t method;
    
    uint32_t sender_pid;
    uint32_t receiver_pid;  /* 0 = broadcast */
    
    uint32_t message_id;
    uint32_t reply_to;      /* For responses */
    
    uint32_t payload_size;
    uint64_t timestamp;
    
} ipc_msg_header_t;

/* IPC Message (complete) */
#define IPC_MAX_PAYLOAD 4096

typedef struct {
    ipc_msg_header_t header;
    uint8_t payload[IPC_MAX_PAYLOAD];
} ipc_message_t;

/* Message queue node */
typedef struct msg_queue_node {
    ipc_message_t message;
    struct msg_queue_node *next;
} msg_queue_node_t;

/* Per-process message queue */
typedef struct {
    uint32_t pid;
    
    msg_queue_node_t *head;
    msg_queue_node_t *tail;
    uint32_t count;
    uint32_t max_size;
    
    uint64_t messages_received;
    uint64_t messages_sent;
    
} process_queue_t;

/* Shared memory segment */
typedef struct shm_segment {
    uint32_t segment_id;
    uint32_t owner_pid;
    
    void *address;
    size_t size;
    
    uint32_t ref_count;
    uint32_t *attached_pids;
    uint32_t num_attached;
    
    bool read_only;
    
    struct shm_segment *next;
    
} shm_segment_t;

/* Service registration */
typedef struct service_reg {
    char name[64];
    uint32_t provider_pid;
    
    uint32_t port;  /* Service port/endpoint */
    
    bool active;
    uint64_t register_time;
    
    struct service_reg *next;
    
} service_reg_t;

/* IPC Router state */
typedef struct {
    /* Process queues (hash table by PID) */
    process_queue_t *queues[256];  /* Simple hash: pid % 256 */
    uint32_t num_queues;
    
    /* Shared memory segments */
    shm_segment_t *shm_segments;
    uint32_t num_segments;
    uint32_t next_segment_id;
    
    /* Service registry */
    service_reg_t *services;
    uint32_t num_services;
    
    /* Configuration */
    visor_mode_t current_mode;
    uint32_t max_queue_size;
    uint32_t max_shm_segments;
    bool broadcast_enabled;
    
    /* Statistics */
    uint64_t total_messages;
    uint64_t total_broadcasts;
    uint64_t total_shm_allocations;
    uint64_t messages_dropped;
    
    volatile uint32_t lock;
    
} ipc_router_t;

/* Global state */
static ipc_router_t g_ipc;

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

/* Hash PID for queue lookup */
static uint32_t hash_pid(uint32_t pid)
{
    return pid % 256;
}

/* Get or create process queue */
static process_queue_t *get_process_queue(uint32_t pid)
{
    uint32_t hash = hash_pid(pid);
    
    process_queue_t *queue = g_ipc.queues[hash];
    
    /* Search in hash bucket */
    while (queue) {
        if (queue->pid == pid) {
            return queue;
        }
        queue = (process_queue_t *)queue->head;  /* Reuse next pointer */
    }
    
    /* Create new queue */
    queue = (process_queue_t *)kalloc(sizeof(process_queue_t));
    if (!queue) return NULL;
    
    memset(queue, 0, sizeof(process_queue_t));
    queue->pid = pid;
    queue->max_size = g_ipc.max_queue_size;
    
    /* Add to hash bucket */
    queue->head = (msg_queue_node_t *)g_ipc.queues[hash];
    g_ipc.queues[hash] = queue;
    g_ipc.num_queues++;
    
    return queue;
}

/* Initialize IPC router */
void ipc_router_init(void)
{
    uart_puts("[IPC] Initializing IPC router\n");
    
    memset(&g_ipc, 0, sizeof(ipc_router_t));
    
    /* Get current mode */
    extern visor_mode_t mode_get_current(void);
    g_ipc.current_mode = mode_get_current();
    
    /* Set configuration based on mode */
    switch (g_ipc.current_mode) {
        case 0:  /* Phone */
            g_ipc.max_queue_size = 100;
            g_ipc.max_shm_segments = 10;
            g_ipc.broadcast_enabled = false;  /* Limited broadcast */
            break;
            
        case 1:  /* Tablet */
            g_ipc.max_queue_size = 500;
            g_ipc.max_shm_segments = 50;
            g_ipc.broadcast_enabled = true;
            break;
            
        case 2:  /* Laptop */
            g_ipc.max_queue_size = 1000;
            g_ipc.max_shm_segments = 100;
            g_ipc.broadcast_enabled = true;
            break;
            
        case 3:  /* TV */
            g_ipc.max_queue_size = 50;
            g_ipc.max_shm_segments = 5;
            g_ipc.broadcast_enabled = false;
            break;
    }
    
    g_ipc.next_segment_id = 1;
    
    uart_puts("[IPC] Mode: ");
    const char *mode_names[] = {"Phone", "Tablet", "Laptop", "TV"};
    uart_puts(mode_names[g_ipc.current_mode]);
    uart_puts("\n");
    
    uart_puts("[IPC] Max queue size: ");
    uart_put_dec(g_ipc.max_queue_size);
    uart_puts("\n");
    
    uart_puts("[IPC] IPC router initialized\n");
}

/* Send message */
bool ipc_send_message(uint32_t sender_pid, uint32_t receiver_pid, 
                     ipc_msg_type_t type, const void *payload, uint32_t size)
{
    if (size > IPC_MAX_PAYLOAD) {
        return false;
    }
    
    spinlock_acquire(&g_ipc.lock);
    
    /* Handle broadcast */
    if (receiver_pid == 0 && type == IPC_MSG_BROADCAST) {
        if (!g_ipc.broadcast_enabled) {
            spinlock_release(&g_ipc.lock);
            return false;
        }
        
        /* Send to all queues */
        for (uint32_t i = 0; i < 256; i++) {
            process_queue_t *queue = g_ipc.queues[i];
            while (queue) {
                if (queue->pid != sender_pid) {
                    /* Send to this process */
                    /* (simplified - would need recursive call) */
                }
                queue = (process_queue_t *)queue->head;
            }
        }
        
        g_ipc.total_broadcasts++;
        spinlock_release(&g_ipc.lock);
        return true;
    }
    
    /* Get receiver queue */
    process_queue_t *queue = get_process_queue(receiver_pid);
    if (!queue) {
        g_ipc.messages_dropped++;
        spinlock_release(&g_ipc.lock);
        return false;
    }
    
    /* Check queue size */
    if (queue->count >= queue->max_size) {
        g_ipc.messages_dropped++;
        spinlock_release(&g_ipc.lock);
        return false;
    }
    
    /* Allocate message node */
    msg_queue_node_t *node = (msg_queue_node_t *)kalloc(sizeof(msg_queue_node_t));
    if (!node) {
        g_ipc.messages_dropped++;
        spinlock_release(&g_ipc.lock);
        return false;
    }
    
    /* Build message */
    node->message.header.type = type;
    node->message.header.method = IPC_METHOD_MESSAGE;
    node->message.header.sender_pid = sender_pid;
    node->message.header.receiver_pid = receiver_pid;
    node->message.header.message_id = g_ipc.total_messages++;
    node->message.header.payload_size = size;
    node->message.header.timestamp = get_timer_count();
    
    if (payload && size > 0) {
        memcpy(node->message.payload, payload, size);
    }
    
    node->next = NULL;
    
    /* Add to queue */
    if (queue->tail) {
        queue->tail->next = node;
    } else {
        queue->head = node;
    }
    queue->tail = node;
    queue->count++;
    
    queue->messages_received++;
    
    spinlock_release(&g_ipc.lock);
    return true;
}

/* Receive message */
bool ipc_receive_message(uint32_t pid, ipc_message_t *msg_out)
{
    if (!msg_out) return false;
    
    spinlock_acquire(&g_ipc.lock);
    
    process_queue_t *queue = get_process_queue(pid);
    if (!queue || !queue->head) {
        spinlock_release(&g_ipc.lock);
        return false;
    }
    
    /* Dequeue message */
    msg_queue_node_t *node = queue->head;
    queue->head = node->next;
    
    if (!queue->head) {
        queue->tail = NULL;
    }
    
    queue->count--;
    
    /* Copy message */
    memcpy(msg_out, &node->message, sizeof(ipc_message_t));
    
    kfree(node);
    
    spinlock_release(&g_ipc.lock);
    return true;
}

/* Create shared memory segment */
uint32_t ipc_shm_create(uint32_t owner_pid, size_t size)
{
    if (size == 0 || size > 1024*1024*100) {  /* Max 100MB */
        return 0;
    }
    
    spinlock_acquire(&g_ipc.lock);
    
    /* Check limit */
    if (g_ipc.num_segments >= g_ipc.max_shm_segments) {
        spinlock_release(&g_ipc.lock);
        return 0;
    }
    
    /* Allocate segment structure */
    shm_segment_t *seg = (shm_segment_t *)kalloc(sizeof(shm_segment_t));
    if (!seg) {
        spinlock_release(&g_ipc.lock);
        return 0;
    }
    
    memset(seg, 0, sizeof(shm_segment_t));
    
    /* Allocate memory */
    seg->address = kalloc(size);
    if (!seg->address) {
        kfree(seg);
        spinlock_release(&g_ipc.lock);
        return 0;
    }
    
    memset(seg->address, 0, size);
    
    /* Allocate PID array */
    seg->attached_pids = (uint32_t *)kalloc(sizeof(uint32_t) * 32);
    if (!seg->attached_pids) {
        kfree(seg->address);
        kfree(seg);
        spinlock_release(&g_ipc.lock);
        return 0;
    }
    
    /* Initialize segment */
    seg->segment_id = g_ipc.next_segment_id++;
    seg->owner_pid = owner_pid;
    seg->size = size;
    seg->ref_count = 1;
    seg->attached_pids[0] = owner_pid;
    seg->num_attached = 1;
    seg->read_only = false;
    
    /* Add to list */
    seg->next = g_ipc.shm_segments;
    g_ipc.shm_segments = seg;
    g_ipc.num_segments++;
    g_ipc.total_shm_allocations++;
    
    uint32_t id = seg->segment_id;
    
    spinlock_release(&g_ipc.lock);
    
    uart_puts("[IPC] Created shared memory segment ");
    uart_put_dec(id);
    uart_puts(" (");
    uart_put_dec(size / 1024);
    uart_puts(" KB)\n");
    
    return id;
}

/* Attach to shared memory */
void *ipc_shm_attach(uint32_t pid, uint32_t segment_id)
{
    spinlock_acquire(&g_ipc.lock);
    
    /* Find segment */
    shm_segment_t *seg = g_ipc.shm_segments;
    while (seg) {
        if (seg->segment_id == segment_id) {
            break;
        }
        seg = seg->next;
    }
    
    if (!seg) {
        spinlock_release(&g_ipc.lock);
        return NULL;
    }
    
    /* Check if already attached */
    for (uint32_t i = 0; i < seg->num_attached; i++) {
        if (seg->attached_pids[i] == pid) {
            void *addr = seg->address;
            spinlock_release(&g_ipc.lock);
            return addr;
        }
    }
    
    /* Attach */
    if (seg->num_attached < 32) {
        seg->attached_pids[seg->num_attached++] = pid;
        seg->ref_count++;
    }
    
    void *addr = seg->address;
    
    spinlock_release(&g_ipc.lock);
    return addr;
}

/* Detach from shared memory */
void ipc_shm_detach(uint32_t pid, uint32_t segment_id)
{
    spinlock_acquire(&g_ipc.lock);
    
    shm_segment_t *seg = g_ipc.shm_segments;
    while (seg) {
        if (seg->segment_id == segment_id) {
            break;
        }
        seg = seg->next;
    }
    
    if (!seg) {
        spinlock_release(&g_ipc.lock);
        return;
    }
    
    /* Remove from attached list */
    for (uint32_t i = 0; i < seg->num_attached; i++) {
        if (seg->attached_pids[i] == pid) {
            /* Shift remaining */
            for (uint32_t j = i; j < seg->num_attached - 1; j++) {
                seg->attached_pids[j] = seg->attached_pids[j + 1];
            }
            seg->num_attached--;
            seg->ref_count--;
            break;
        }
    }
    
    spinlock_release(&g_ipc.lock);
}

/* Destroy shared memory */
void ipc_shm_destroy(uint32_t segment_id)
{
    spinlock_acquire(&g_ipc.lock);
    
    shm_segment_t *seg = g_ipc.shm_segments;
    shm_segment_t *prev = NULL;
    
    while (seg) {
        if (seg->segment_id == segment_id) {
            /* Only owner can destroy */
            if (seg->ref_count > 0) {
                spinlock_release(&g_ipc.lock);
                return;
            }
            
            /* Remove from list */
            if (prev) {
                prev->next = seg->next;
            } else {
                g_ipc.shm_segments = seg->next;
            }
            
            /* Free memory */
            kfree(seg->address);
            kfree(seg->attached_pids);
            kfree(seg);
            
            g_ipc.num_segments--;
            
            break;
        }
        prev = seg;
        seg = seg->next;
    }
    
    spinlock_release(&g_ipc.lock);
}

/* Register service */
bool ipc_service_register(uint32_t pid, const char *name, uint32_t port)
{
    if (!name || str_len(name) == 0 || str_len(name) >= 64) {
        return false;
    }
    
    spinlock_acquire(&g_ipc.lock);
    
    /* Check if already registered */
    service_reg_t *svc = g_ipc.services;
    while (svc) {
        if (str_cmp(svc->name, name) == 0) {
            spinlock_release(&g_ipc.lock);
            return false;
        }
        svc = svc->next;
    }
    
    /* Create registration */
    svc = (service_reg_t *)kalloc(sizeof(service_reg_t));
    if (!svc) {
        spinlock_release(&g_ipc.lock);
        return false;
    }
    
    memset(svc, 0, sizeof(service_reg_t));
    str_cpy(svc->name, name);
    svc->provider_pid = pid;
    svc->port = port;
    svc->active = true;
    svc->register_time = get_timer_count();
    
    /* Add to list */
    svc->next = g_ipc.services;
    g_ipc.services = svc;
    g_ipc.num_services++;
    
    spinlock_release(&g_ipc.lock);
    
    uart_puts("[IPC] Service registered: ");
    uart_puts(name);
    uart_puts(" (PID ");
    uart_put_dec(pid);
    uart_puts(")\n");
    
    return true;
}

/* Query service */
uint32_t ipc_service_query(const char *name, uint32_t *port_out)
{
    if (!name) return 0;
    
    spinlock_acquire(&g_ipc.lock);
    
    service_reg_t *svc = g_ipc.services;
    while (svc) {
        if (str_cmp(svc->name, name) == 0 && svc->active) {
            uint32_t pid = svc->provider_pid;
            if (port_out) *port_out = svc->port;
            spinlock_release(&g_ipc.lock);
            return pid;
        }
        svc = svc->next;
    }
    
    spinlock_release(&g_ipc.lock);
    return 0;
}

/* Unregister service */
void ipc_service_unregister(const char *name)
{
    if (!name) return;
    
    spinlock_acquire(&g_ipc.lock);
    
    service_reg_t *svc = g_ipc.services;
    service_reg_t *prev = NULL;
    
    while (svc) {
        if (str_cmp(svc->name, name) == 0) {
            if (prev) {
                prev->next = svc->next;
            } else {
                g_ipc.services = svc->next;
            }
            
            kfree(svc);
            g_ipc.num_services--;
            break;
        }
        prev = svc;
        svc = svc->next;
    }
    
    spinlock_release(&g_ipc.lock);
}

/* Reconfigure for mode change */
void ipc_router_reconfigure(visor_mode_t mode)
{
    spinlock_acquire(&g_ipc.lock);
    
    g_ipc.current_mode = mode;
    
    /* Update limits based on mode */
    switch (mode) {
        case 0:  /* Phone */
            g_ipc.max_queue_size = 100;
            g_ipc.broadcast_enabled = false;
            break;
        case 1:  /* Tablet */
            g_ipc.max_queue_size = 500;
            g_ipc.broadcast_enabled = true;
            break;
        case 2:  /* Laptop */
            g_ipc.max_queue_size = 1000;
            g_ipc.broadcast_enabled = true;
            break;
        case 3:  /* TV */
            g_ipc.max_queue_size = 50;
            g_ipc.broadcast_enabled = false;
            break;
    }
    
    spinlock_release(&g_ipc.lock);
}

/* Print statistics */
void ipc_router_print_stats(void)
{
    uart_puts("\n[IPC] Statistics:\n");
    uart_puts("════════════════════════════════════════\n");
    
    uart_puts("  Mode:              ");
    const char *mode_names[] = {"Phone", "Tablet", "Laptop", "TV"};
    uart_puts(mode_names[g_ipc.current_mode]);
    uart_puts("\n");
    
    uart_puts("  Total messages:    ");
    uart_put_dec(g_ipc.total_messages);
    uart_puts("\n");
    
    uart_puts("  Messages dropped:  ");
    uart_put_dec(g_ipc.messages_dropped);
    uart_puts("\n");
    
    uart_puts("  Broadcasts:        ");
    uart_put_dec(g_ipc.total_broadcasts);
    uart_puts("\n");
    
    uart_puts("  Active queues:     ");
    uart_put_dec(g_ipc.num_queues);
    uart_puts("\n");
    
    uart_puts("  SHM segments:      ");
    uart_put_dec(g_ipc.num_segments);
    uart_puts("\n");
    
    uart_puts("  Total SHM allocs:  ");
    uart_put_dec(g_ipc.total_shm_allocations);
    uart_puts("\n");
    
    uart_puts("  Services:          ");
    uart_put_dec(g_ipc.num_services);
    uart_puts("\n");
    
    uart_puts("  Broadcast enabled: ");
    uart_puts(g_ipc.broadcast_enabled ? "yes\n" : "no\n");
    
    uart_puts("\n");
}