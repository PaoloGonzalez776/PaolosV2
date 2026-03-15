/*
 * shared_memory_manager.c - Shared Memory Manager
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Sistema completo de gestión de memoria compartida para IPC de alto rendimiento.
 * Zero-copy data sharing entre procesos.
 * 
 * CARACTERÍSTICAS:
 * - Create/destroy segments
 * - Attach/detach operations
 * - Reference counting (automatic cleanup)
 * - Access control (read-only, read-write)
 * - Named segments (key-based lookup)
 * - Permission checking
 * - Memory alignment (page-aligned)
 * - Automatic cleanup on process death
 * - Statistics tracking
 * 
 * INSPIRATION: POSIX shm*, System V IPC
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

/* Access permissions */
#define SHM_R           0x0001  /* Read permission */
#define SHM_W           0x0002  /* Write permission */

#define SHM_RDONLY      0x1000  /* Attach read-only */
#define SHM_RND         0x2000  /* Round attach address */

/* Special keys */
#define SHM_KEY_PRIVATE 0       /* Private segment (no key) */

/* Limits */
#define SHM_MAX_SEGMENTS    256
#define SHM_MAX_SIZE        (100 * 1024 * 1024)  /* 100MB */
#define SHM_MIN_SIZE        4096                  /* 4KB */
#define SHM_PAGE_SIZE       4096                  /* 4KB pages */
#define SHM_MAX_ATTACH      32                    /* Max processes per segment */

/* Segment state */
typedef enum {
    SHM_STATE_FREE       = 0,
    SHM_STATE_ALLOCATED  = 1,
    SHM_STATE_MARKED_DELETE = 2,  /* Marked for deletion */
} shm_state_t;

/* Attached process info */
typedef struct {
    uint32_t pid;
    void *attach_addr;
    uint32_t flags;
    uint64_t attach_time;
} shm_attach_info_t;

/* Shared memory segment descriptor */
typedef struct {
    uint32_t segment_id;
    uint32_t key;              /* Named key (0 = private) */
    shm_state_t state;
    
    /* Owner */
    uint32_t owner_pid;
    uint32_t creator_pid;
    
    /* Memory */
    void *address;
    size_t size;
    size_t actual_size;        /* Rounded to pages */
    
    /* Access control */
    uint32_t permissions;      /* SHM_R | SHM_W */
    
    /* Attachment tracking */
    shm_attach_info_t attachments[SHM_MAX_ATTACH];
    uint32_t num_attached;
    uint32_t attach_count;     /* Total attachments ever */
    
    /* Timestamps */
    uint64_t create_time;
    uint64_t last_attach_time;
    uint64_t last_detach_time;
    
    /* Reference counting */
    uint32_t ref_count;
    
} shm_segment_t;

/* Shared memory manager state */
typedef struct {
    /* Segment table */
    shm_segment_t segments[SHM_MAX_SEGMENTS];
    uint32_t num_segments;
    uint32_t next_segment_id;
    
    /* Configuration */
    size_t max_total_size;
    size_t current_total_size;
    
    /* Statistics */
    uint64_t total_segments_created;
    uint64_t total_segments_destroyed;
    uint64_t total_attaches;
    uint64_t total_detaches;
    uint64_t total_bytes_allocated;
    
    volatile uint32_t lock;
    
} shm_manager_t;

/* Global state */
static shm_manager_t g_shm;

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

/* Round up to page size */
static size_t round_to_page(size_t size)
{
    return (size + SHM_PAGE_SIZE - 1) & ~(SHM_PAGE_SIZE - 1);
}

/* Find segment by ID */
static shm_segment_t *find_segment_by_id(uint32_t segment_id)
{
    for (uint32_t i = 0; i < SHM_MAX_SEGMENTS; i++) {
        if (g_shm.segments[i].state != SHM_STATE_FREE &&
            g_shm.segments[i].segment_id == segment_id) {
            return &g_shm.segments[i];
        }
    }
    return NULL;
}

/* Find segment by key */
static shm_segment_t *find_segment_by_key(uint32_t key)
{
    if (key == SHM_KEY_PRIVATE) {
        return NULL;
    }
    
    for (uint32_t i = 0; i < SHM_MAX_SEGMENTS; i++) {
        if (g_shm.segments[i].state != SHM_STATE_FREE &&
            g_shm.segments[i].key == key) {
            return &g_shm.segments[i];
        }
    }
    return NULL;
}

/* Find free segment slot */
static shm_segment_t *alloc_segment_slot(void)
{
    for (uint32_t i = 0; i < SHM_MAX_SEGMENTS; i++) {
        if (g_shm.segments[i].state == SHM_STATE_FREE) {
            return &g_shm.segments[i];
        }
    }
    return NULL;
}

/* Check if PID is attached to segment */
static bool is_attached(shm_segment_t *seg, uint32_t pid)
{
    for (uint32_t i = 0; i < seg->num_attached; i++) {
        if (seg->attachments[i].pid == pid) {
            return true;
        }
    }
    return false;
}

/* Check permissions */
static bool check_permission(shm_segment_t *seg, uint32_t pid, uint32_t required_perm)
{
    /* Owner has all permissions */
    if (seg->owner_pid == pid) {
        return true;
    }
    
    /* Check segment permissions */
    return (seg->permissions & required_perm) == required_perm;
}

/* Initialize shared memory manager */
void shm_init(void)
{
    uart_puts("[SHM] Initializing shared memory manager\n");
    
    memset(&g_shm, 0, sizeof(shm_manager_t));
    
    /* Set limits based on mode */
    extern visor_mode_t mode_get_current(void);
    visor_mode_t mode = mode_get_current();
    
    switch (mode) {
        case 0:  /* Phone */
            g_shm.max_total_size = 50 * 1024 * 1024;  /* 50MB */
            break;
        case 1:  /* Tablet */
            g_shm.max_total_size = 200 * 1024 * 1024;  /* 200MB */
            break;
        case 2:  /* Laptop */
            g_shm.max_total_size = 500 * 1024 * 1024;  /* 500MB */
            break;
        case 3:  /* TV */
            g_shm.max_total_size = 100 * 1024 * 1024;  /* 100MB */
            break;
    }
    
    g_shm.next_segment_id = 1;
    
    /* Mark all segments as free */
    for (uint32_t i = 0; i < SHM_MAX_SEGMENTS; i++) {
        g_shm.segments[i].state = SHM_STATE_FREE;
    }
    
    uart_puts("[SHM] Max total size: ");
    uart_put_dec(g_shm.max_total_size / (1024 * 1024));
    uart_puts(" MB\n");
    
    uart_puts("[SHM] Shared memory manager initialized\n");
}

/* Create shared memory segment */
uint32_t shm_create(uint32_t pid, uint32_t key, size_t size, uint32_t permissions)
{
    if (size < SHM_MIN_SIZE || size > SHM_MAX_SIZE) {
        return 0;
    }
    
    spinlock_acquire(&g_shm.lock);
    
    /* Check if key already exists */
    if (key != SHM_KEY_PRIVATE) {
        shm_segment_t *existing = find_segment_by_key(key);
        if (existing) {
            spinlock_release(&g_shm.lock);
            return 0;  /* Key already exists */
        }
    }
    
    /* Check total size limit */
    size_t actual_size = round_to_page(size);
    
    if (g_shm.current_total_size + actual_size > g_shm.max_total_size) {
        spinlock_release(&g_shm.lock);
        return 0;  /* Would exceed limit */
    }
    
    /* Find free slot */
    shm_segment_t *seg = alloc_segment_slot();
    if (!seg) {
        spinlock_release(&g_shm.lock);
        return 0;  /* No free slots */
    }
    
    /* Allocate memory (page-aligned) */
    void *mem = kalloc(actual_size);
    if (!mem) {
        spinlock_release(&g_shm.lock);
        return 0;  /* Out of memory */
    }
    
    memset(mem, 0, actual_size);
    
    /* Initialize segment */
    memset(seg, 0, sizeof(shm_segment_t));
    
    seg->segment_id = g_shm.next_segment_id++;
    seg->key = key;
    seg->state = SHM_STATE_ALLOCATED;
    seg->owner_pid = pid;
    seg->creator_pid = pid;
    seg->address = mem;
    seg->size = size;
    seg->actual_size = actual_size;
    seg->permissions = permissions;
    seg->create_time = get_timer_count();
    seg->ref_count = 0;
    seg->num_attached = 0;
    
    /* Update statistics */
    g_shm.num_segments++;
    g_shm.current_total_size += actual_size;
    g_shm.total_segments_created++;
    g_shm.total_bytes_allocated += actual_size;
    
    uint32_t id = seg->segment_id;
    
    spinlock_release(&g_shm.lock);
    
    uart_puts("[SHM] Created segment ");
    uart_put_dec(id);
    uart_puts(" (");
    uart_put_dec(size / 1024);
    uart_puts(" KB");
    if (key != SHM_KEY_PRIVATE) {
        uart_puts(", key=");
        uart_put_hex(key);
    }
    uart_puts(")\n");
    
    return id;
}

/* Get segment by key (or create if doesn't exist) */
uint32_t shm_get(uint32_t pid, uint32_t key, size_t size, uint32_t flags)
{
    spinlock_acquire(&g_shm.lock);
    
    /* Try to find existing */
    shm_segment_t *seg = find_segment_by_key(key);
    
    if (seg) {
        /* Segment exists */
        if (size > seg->size) {
            spinlock_release(&g_shm.lock);
            return 0;  /* Requested size too large */
        }
        
        uint32_t id = seg->segment_id;
        spinlock_release(&g_shm.lock);
        return id;
    }
    
    /* Doesn't exist - create if IPC_CREAT flag */
    spinlock_release(&g_shm.lock);
    
    /* For simplicity, always create */
    return shm_create(pid, key, size, SHM_R | SHM_W);
}

/* Attach to shared memory */
void *shm_attach(uint32_t pid, uint32_t segment_id, uint32_t flags)
{
    spinlock_acquire(&g_shm.lock);
    
    shm_segment_t *seg = find_segment_by_id(segment_id);
    if (!seg) {
        spinlock_release(&g_shm.lock);
        return NULL;
    }
    
    /* Check if already attached */
    if (is_attached(seg, pid)) {
        /* Return existing attachment address */
        for (uint32_t i = 0; i < seg->num_attached; i++) {
            if (seg->attachments[i].pid == pid) {
                void *addr = seg->attachments[i].attach_addr;
                spinlock_release(&g_shm.lock);
                return addr;
            }
        }
    }
    
    /* Check permissions */
    uint32_t required_perm = (flags & SHM_RDONLY) ? SHM_R : (SHM_R | SHM_W);
    if (!check_permission(seg, pid, required_perm)) {
        spinlock_release(&g_shm.lock);
        return NULL;  /* Permission denied */
    }
    
    /* Check attachment limit */
    if (seg->num_attached >= SHM_MAX_ATTACH) {
        spinlock_release(&g_shm.lock);
        return NULL;  /* Too many attachments */
    }
    
    /* Add attachment */
    uint32_t idx = seg->num_attached;
    seg->attachments[idx].pid = pid;
    seg->attachments[idx].attach_addr = seg->address;
    seg->attachments[idx].flags = flags;
    seg->attachments[idx].attach_time = get_timer_count();
    
    seg->num_attached++;
    seg->attach_count++;
    seg->ref_count++;
    seg->last_attach_time = get_timer_count();
    
    g_shm.total_attaches++;
    
    void *addr = seg->address;
    
    spinlock_release(&g_shm.lock);
    
    return addr;
}

/* Detach from shared memory */
bool shm_detach(uint32_t pid, uint32_t segment_id)
{
    spinlock_acquire(&g_shm.lock);
    
    shm_segment_t *seg = find_segment_by_id(segment_id);
    if (!seg) {
        spinlock_release(&g_shm.lock);
        return false;
    }
    
    /* Find and remove attachment */
    bool found = false;
    for (uint32_t i = 0; i < seg->num_attached; i++) {
        if (seg->attachments[i].pid == pid) {
            /* Shift remaining attachments */
            for (uint32_t j = i; j < seg->num_attached - 1; j++) {
                seg->attachments[j] = seg->attachments[j + 1];
            }
            
            seg->num_attached--;
            seg->ref_count--;
            seg->last_detach_time = get_timer_count();
            
            g_shm.total_detaches++;
            
            found = true;
            break;
        }
    }
    
    /* Check if segment should be destroyed */
    if (seg->state == SHM_STATE_MARKED_DELETE && seg->ref_count == 0) {
        /* Destroy segment */
        kfree(seg->address);
        
        g_shm.current_total_size -= seg->actual_size;
        g_shm.num_segments--;
        g_shm.total_segments_destroyed++;
        
        seg->state = SHM_STATE_FREE;
        
        uart_puts("[SHM] Auto-destroyed segment ");
        uart_put_dec(segment_id);
        uart_puts(" (ref_count=0)\n");
    }
    
    spinlock_release(&g_shm.lock);
    return found;
}

/* Destroy shared memory segment */
bool shm_destroy(uint32_t pid, uint32_t segment_id)
{
    spinlock_acquire(&g_shm.lock);
    
    shm_segment_t *seg = find_segment_by_id(segment_id);
    if (!seg) {
        spinlock_release(&g_shm.lock);
        return false;
    }
    
    /* Only owner can destroy */
    if (seg->owner_pid != pid) {
        spinlock_release(&g_shm.lock);
        return false;
    }
    
    if (seg->ref_count > 0) {
        /* Still attached - mark for deletion */
        seg->state = SHM_STATE_MARKED_DELETE;
        
        spinlock_release(&g_shm.lock);
        
        uart_puts("[SHM] Marked segment ");
        uart_put_dec(segment_id);
        uart_puts(" for deletion (ref_count=");
        uart_put_dec(seg->ref_count);
        uart_puts(")\n");
        
        return true;
    }
    
    /* No attachments - destroy immediately */
    kfree(seg->address);
    
    g_shm.current_total_size -= seg->actual_size;
    g_shm.num_segments--;
    g_shm.total_segments_destroyed++;
    
    seg->state = SHM_STATE_FREE;
    
    spinlock_release(&g_shm.lock);
    
    uart_puts("[SHM] Destroyed segment ");
    uart_put_dec(segment_id);
    uart_puts("\n");
    
    return true;
}

/* Cleanup all segments for a dead process */
void shm_cleanup_process(uint32_t pid)
{
    spinlock_acquire(&g_shm.lock);
    
    /* Detach from all segments */
    for (uint32_t i = 0; i < SHM_MAX_SEGMENTS; i++) {
        shm_segment_t *seg = &g_shm.segments[i];
        
        if (seg->state == SHM_STATE_FREE) {
            continue;
        }
        
        /* Check if attached */
        for (uint32_t j = 0; j < seg->num_attached; j++) {
            if (seg->attachments[j].pid == pid) {
                /* Remove attachment */
                for (uint32_t k = j; k < seg->num_attached - 1; k++) {
                    seg->attachments[k] = seg->attachments[k + 1];
                }
                
                seg->num_attached--;
                seg->ref_count--;
                j--;  /* Re-check this index */
            }
        }
        
        /* If owner died, transfer ownership or destroy */
        if (seg->owner_pid == pid) {
            if (seg->num_attached > 0) {
                /* Transfer to first attached process */
                seg->owner_pid = seg->attachments[0].pid;
            } else {
                /* No attachments - destroy */
                kfree(seg->address);
                
                g_shm.current_total_size -= seg->actual_size;
                g_shm.num_segments--;
                g_shm.total_segments_destroyed++;
                
                seg->state = SHM_STATE_FREE;
            }
        }
    }
    
    spinlock_release(&g_shm.lock);
    
    uart_puts("[SHM] Cleaned up segments for PID ");
    uart_put_dec(pid);
    uart_puts("\n");
}

/* Get segment info */
bool shm_get_info(uint32_t segment_id, size_t *size_out, uint32_t *num_attached_out)
{
    spinlock_acquire(&g_shm.lock);
    
    shm_segment_t *seg = find_segment_by_id(segment_id);
    if (!seg) {
        spinlock_release(&g_shm.lock);
        return false;
    }
    
    if (size_out) *size_out = seg->size;
    if (num_attached_out) *num_attached_out = seg->num_attached;
    
    spinlock_release(&g_shm.lock);
    return true;
}

/* List all segments */
void shm_list_segments(void)
{
    uart_puts("\n[SHM] Active Segments:\n");
    uart_puts("════════════════════════════════════════\n");
    
    spinlock_acquire(&g_shm.lock);
    
    for (uint32_t i = 0; i < SHM_MAX_SEGMENTS; i++) {
        shm_segment_t *seg = &g_shm.segments[i];
        
        if (seg->state == SHM_STATE_FREE) {
            continue;
        }
        
        uart_puts("  Segment ");
        uart_put_dec(seg->segment_id);
        uart_puts(":\n");
        
        uart_puts("    Size:       ");
        uart_put_dec(seg->size / 1024);
        uart_puts(" KB\n");
        
        uart_puts("    Owner PID:  ");
        uart_put_dec(seg->owner_pid);
        uart_puts("\n");
        
        uart_puts("    Attached:   ");
        uart_put_dec(seg->num_attached);
        uart_puts("\n");
        
        if (seg->key != SHM_KEY_PRIVATE) {
            uart_puts("    Key:        ");
            uart_put_hex(seg->key);
            uart_puts("\n");
        }
        
        if (seg->state == SHM_STATE_MARKED_DELETE) {
            uart_puts("    State:      MARKED FOR DELETE\n");
        }
        
        uart_puts("\n");
    }
    
    spinlock_release(&g_shm.lock);
}

/* Print statistics */
void shm_print_stats(void)
{
    uart_puts("\n[SHM] Statistics:\n");
    uart_puts("════════════════════════════════════════\n");
    
    uart_puts("  Active segments:   ");
    uart_put_dec(g_shm.num_segments);
    uart_puts("\n");
    
    uart_puts("  Current size:      ");
    uart_put_dec(g_shm.current_total_size / (1024 * 1024));
    uart_puts(" MB\n");
    
    uart_puts("  Max size:          ");
    uart_put_dec(g_shm.max_total_size / (1024 * 1024));
    uart_puts(" MB\n");
    
    uart_puts("  Created:           ");
    uart_put_dec(g_shm.total_segments_created);
    uart_puts("\n");
    
    uart_puts("  Destroyed:         ");
    uart_put_dec(g_shm.total_segments_destroyed);
    uart_puts("\n");
    
    uart_puts("  Total attaches:    ");
    uart_put_dec(g_shm.total_attaches);
    uart_puts("\n");
    
    uart_puts("  Total detaches:    ");
    uart_put_dec(g_shm.total_detaches);
    uart_puts("\n");
    
    uart_puts("  Bytes allocated:   ");
    uart_put_dec(g_shm.total_bytes_allocated / (1024 * 1024));
    uart_puts(" MB\n");
    
    uart_puts("\n");
}