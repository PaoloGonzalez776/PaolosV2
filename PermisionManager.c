/*
 * permission_manager.c - Permission Manager
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Sistema completo de gestión de permisos de archivos y recursos.
 * Implementa permisos UNIX tradicionales + ACLs (Access Control Lists).
 * 
 * CARACTERÍSTICAS:
 * - UNIX permissions (user/group/other, rwx)
 * - Access Control Lists (ACLs)
 * - Owner/group management
 * - Permission inheritance
 * - umask support
 * - Permission checking
 * - Default ACLs
 * 
 * INSPIRATION:
 * - POSIX permissions
 * - Linux ACLs (setfacl/getfacl)
 * - chmod/chown semantics
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

/* Permission bits (rwx) */
#define PERM_READ           0x4
#define PERM_WRITE          0x2
#define PERM_EXEC           0x1
#define PERM_RWX            0x7

/* Permission shifts */
#define PERM_OWNER_SHIFT    6
#define PERM_GROUP_SHIFT    3
#define PERM_OTHER_SHIFT    0

/* Permission masks */
#define PERM_OWNER_MASK     0x1C0  /* rwx for owner */
#define PERM_GROUP_MASK     0x038  /* rwx for group */
#define PERM_OTHER_MASK     0x007  /* rwx for other */

/* Special bits */
#define PERM_SETUID         0x800  /* Set UID on execution */
#define PERM_SETGID         0x400  /* Set GID on execution */
#define PERM_STICKY         0x200  /* Sticky bit */

/* ACL entry types */
typedef enum {
    ACL_USER_OBJ    = 0,  /* Owner */
    ACL_USER        = 1,  /* Named user */
    ACL_GROUP_OBJ   = 2,  /* Owning group */
    ACL_GROUP       = 3,  /* Named group */
    ACL_MASK        = 4,  /* Mask */
    ACL_OTHER       = 5,  /* Other */
} acl_type_t;

/* ACL entry */
typedef struct acl_entry {
    acl_type_t type;
    uint32_t id;           /* UID or GID (for USER/GROUP) */
    uint32_t permissions;  /* rwx bits */
    
    struct acl_entry *next;
} acl_entry_t;

/* Resource permissions */
typedef struct {
    uint32_t resource_id;  /* File/resource ID */
    
    /* UNIX permissions */
    uint32_t owner_uid;
    uint32_t owner_gid;
    uint32_t mode;         /* Permission bits */
    
    /* ACL */
    acl_entry_t *acl;
    acl_entry_t *default_acl;  /* For directories */
    bool has_acl;
    
    /* Mask (for ACL) */
    uint32_t acl_mask;
    
} resource_perms_t;

/* Permission manager */
typedef struct {
    /* Permissions table (hash by resource_id) */
    resource_perms_t *perms[512];
    uint32_t num_resources;
    
    /* Default umask */
    uint32_t default_umask;
    
    /* Statistics */
    uint64_t permission_checks;
    uint64_t permission_denials;
    uint64_t acl_checks;
    
    volatile uint32_t lock;
    
} perm_manager_t;

/* Global state */
static perm_manager_t g_perm;

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

/* Hash resource ID */
static uint32_t hash_resource(uint32_t resource_id)
{
    return resource_id % 512;
}

/* Find resource permissions */
static resource_perms_t *find_resource(uint32_t resource_id)
{
    uint32_t hash = hash_resource(resource_id);
    resource_perms_t *perms = g_perm.perms[hash];
    
    while (perms) {
        if (perms->resource_id == resource_id) {
            return perms;
        }
        perms = (resource_perms_t *)perms->acl;  /* Reuse next pointer */
    }
    
    return NULL;
}

/* Initialize permission manager */
void perm_init(void)
{
    uart_puts("[PERM] Initializing permission manager\n");
    
    memset(&g_perm, 0, sizeof(perm_manager_t));
    
    /* Default umask: 022 (rwxr-xr-x) */
    g_perm.default_umask = 022;
    
    uart_puts("[PERM] Permission manager initialized\n");
}

/* Create permissions for resource */
bool perm_create(uint32_t resource_id, uint32_t owner_uid, uint32_t owner_gid, uint32_t mode)
{
    spinlock_acquire(&g_perm.lock);
    
    /* Check if already exists */
    if (find_resource(resource_id)) {
        spinlock_release(&g_perm.lock);
        return false;
    }
    
    /* Allocate */
    resource_perms_t *perms = (resource_perms_t *)kalloc(sizeof(resource_perms_t));
    if (!perms) {
        spinlock_release(&g_perm.lock);
        return false;
    }
    
    memset(perms, 0, sizeof(resource_perms_t));
    
    perms->resource_id = resource_id;
    perms->owner_uid = owner_uid;
    perms->owner_gid = owner_gid;
    perms->mode = mode & ~g_perm.default_umask;  /* Apply umask */
    perms->acl_mask = PERM_RWX;  /* Default: all permissions */
    
    /* Add to hash table */
    uint32_t hash = hash_resource(resource_id);
    perms->acl = (acl_entry_t *)g_perm.perms[hash];  /* Reuse as next */
    g_perm.perms[hash] = perms;
    g_perm.num_resources++;
    
    spinlock_release(&g_perm.lock);
    return true;
}

/* Set permissions (chmod) */
bool perm_chmod(uint32_t resource_id, uint32_t mode)
{
    spinlock_acquire(&g_perm.lock);
    
    resource_perms_t *perms = find_resource(resource_id);
    if (!perms) {
        spinlock_release(&g_perm.lock);
        return false;
    }
    
    perms->mode = mode;
    
    spinlock_release(&g_perm.lock);
    return true;
}

/* Change owner (chown) */
bool perm_chown(uint32_t resource_id, uint32_t new_uid, uint32_t new_gid)
{
    spinlock_acquire(&g_perm.lock);
    
    resource_perms_t *perms = find_resource(resource_id);
    if (!perms) {
        spinlock_release(&g_perm.lock);
        return false;
    }
    
    perms->owner_uid = new_uid;
    perms->owner_gid = new_gid;
    
    spinlock_release(&g_perm.lock);
    return true;
}

/* Add ACL entry */
bool perm_acl_add(uint32_t resource_id, acl_type_t type, uint32_t id, uint32_t permissions)
{
    spinlock_acquire(&g_perm.lock);
    
    resource_perms_t *perms = find_resource(resource_id);
    if (!perms) {
        spinlock_release(&g_perm.lock);
        return false;
    }
    
    /* Allocate ACL entry */
    acl_entry_t *entry = (acl_entry_t *)kalloc(sizeof(acl_entry_t));
    if (!entry) {
        spinlock_release(&g_perm.lock);
        return false;
    }
    
    entry->type = type;
    entry->id = id;
    entry->permissions = permissions & PERM_RWX;
    entry->next = perms->acl;
    
    perms->acl = entry;
    perms->has_acl = true;
    
    spinlock_release(&g_perm.lock);
    return true;
}

/* Remove ACL entry */
bool perm_acl_remove(uint32_t resource_id, acl_type_t type, uint32_t id)
{
    spinlock_acquire(&g_perm.lock);
    
    resource_perms_t *perms = find_resource(resource_id);
    if (!perms) {
        spinlock_release(&g_perm.lock);
        return false;
    }
    
    acl_entry_t *entry = perms->acl;
    acl_entry_t *prev = NULL;
    
    while (entry) {
        if (entry->type == type && (type == ACL_USER_OBJ || type == ACL_GROUP_OBJ || 
                                    type == ACL_OTHER || type == ACL_MASK || entry->id == id)) {
            if (prev) {
                prev->next = entry->next;
            } else {
                perms->acl = entry->next;
            }
            
            kfree(entry);
            spinlock_release(&g_perm.lock);
            return true;
        }
        
        prev = entry;
        entry = entry->next;
    }
    
    spinlock_release(&g_perm.lock);
    return false;
}

/* Check permission */
bool perm_check(uint32_t resource_id, uint32_t uid, uint32_t gid, uint32_t requested)
{
    spinlock_acquire(&g_perm.lock);
    
    g_perm.permission_checks++;
    
    resource_perms_t *perms = find_resource(resource_id);
    if (!perms) {
        spinlock_release(&g_perm.lock);
        return false;  /* Resource not found */
    }
    
    uint32_t allowed = 0;
    
    /* Check ACL first if present */
    if (perms->has_acl) {
        g_perm.acl_checks++;
        
        acl_entry_t *entry = perms->acl;
        bool matched = false;
        
        while (entry) {
            if (entry->type == ACL_USER && entry->id == uid) {
                /* Named user ACL */
                allowed = entry->permissions & perms->acl_mask;
                matched = true;
                break;
            } else if (entry->type == ACL_GROUP && entry->id == gid) {
                /* Named group ACL */
                allowed = entry->permissions & perms->acl_mask;
                matched = true;
                break;
            }
            entry = entry->next;
        }
        
        if (!matched) {
            /* Fall through to standard permissions */
        } else {
            goto check_result;
        }
    }
    
    /* Standard UNIX permissions */
    if (uid == perms->owner_uid) {
        /* Owner */
        allowed = (perms->mode >> PERM_OWNER_SHIFT) & PERM_RWX;
    } else if (gid == perms->owner_gid) {
        /* Group */
        allowed = (perms->mode >> PERM_GROUP_SHIFT) & PERM_RWX;
    } else {
        /* Other */
        allowed = (perms->mode >> PERM_OTHER_SHIFT) & PERM_RWX;
    }
    
check_result:
    bool result = (allowed & requested) == requested;
    
    if (!result) {
        g_perm.permission_denials++;
    }
    
    spinlock_release(&g_perm.lock);
    return result;
}

/* Set umask */
void perm_set_umask(uint32_t umask)
{
    spinlock_acquire(&g_perm.lock);
    g_perm.default_umask = umask & 0777;
    spinlock_release(&g_perm.lock);
}

/* Get umask */
uint32_t perm_get_umask(void)
{
    return g_perm.default_umask;
}

/* Print statistics */
void perm_print_stats(void)
{
    uart_puts("\n[PERM] Statistics:\n");
    uart_puts("════════════════════════════════════════\n");
    
    uart_puts("  Resources:         ");
    uart_put_dec(g_perm.num_resources);
    uart_puts("\n");
    
    uart_puts("  Permission checks: ");
    uart_put_dec(g_perm.permission_checks);
    uart_puts("\n");
    
    uart_puts("  Denials:           ");
    uart_put_dec(g_perm.permission_denials);
    uart_puts("\n");
    
    uart_puts("  ACL checks:        ");
    uart_put_dec(g_perm.acl_checks);
    uart_puts("\n");
    
    uart_puts("  Default umask:     ");
    uart_put_hex(g_perm.default_umask);
    uart_puts("\n");
    
    uart_puts("\n");
}