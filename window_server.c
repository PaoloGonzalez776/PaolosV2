/*
 * window_server.c - Window Server
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Servidor central de ventanas que gestiona todas las ventanas del sistema.
 * Coordina con el compositor para renderizar, maneja eventos de entrada,
 * y proporciona la API para que las aplicaciones creen y gestionen ventanas.
 * 
 * CARACTERÍSTICAS:
 * - Creación/destrucción de ventanas
 * - Gestión de focus
 * - Z-order (stacking)
 * - Eventos de entrada
 * - Decoraciones de ventanas
 * - Resize/move operations
 * - Minimizar/maximizar/fullscreen
 * - Mode-aware (Phone/Tablet/Laptop/TV)
 * - IPC con aplicaciones
 * - Window properties
 * - Parent/child hierarchy
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

/* Window states */
typedef enum {
    WINDOW_STATE_NORMAL         = 0,
    WINDOW_STATE_MINIMIZED      = 1,
    WINDOW_STATE_MAXIMIZED      = 2,
    WINDOW_STATE_FULLSCREEN     = 3,
    WINDOW_STATE_HIDDEN         = 4,
    WINDOW_STATE_CLOSING        = 5,
} window_state_t;

/* Window types */
typedef enum {
    WINDOW_TYPE_NORMAL          = 0,
    WINDOW_TYPE_DIALOG          = 1,
    WINDOW_TYPE_UTILITY         = 2,
    WINDOW_TYPE_TOOLBAR         = 3,
    WINDOW_TYPE_MENU            = 4,
    WINDOW_TYPE_SPLASH          = 5,
    WINDOW_TYPE_DESKTOP         = 6,
    WINDOW_TYPE_NOTIFICATION    = 7,
} window_type_t;

/* Window flags */
#define WINDOW_FLAG_RESIZABLE       0x0001
#define WINDOW_FLAG_MOVABLE         0x0002
#define WINDOW_FLAG_CLOSABLE        0x0004
#define WINDOW_FLAG_MINIMIZABLE     0x0008
#define WINDOW_FLAG_MAXIMIZABLE     0x0010
#define WINDOW_FLAG_FULLSCREENABLE  0x0020
#define WINDOW_FLAG_DECORATED       0x0040
#define WINDOW_FLAG_MODAL           0x0080
#define WINDOW_FLAG_ALWAYS_ON_TOP   0x0100
#define WINDOW_FLAG_SKIP_TASKBAR    0x0200
#define WINDOW_FLAG_ACCEPTS_INPUT   0x0400
#define WINDOW_FLAG_TRANSPARENT     0x0800

/* Event types */
typedef enum {
    EVENT_WINDOW_CREATE         = 0,
    EVENT_WINDOW_DESTROY        = 1,
    EVENT_WINDOW_CLOSE          = 2,
    EVENT_WINDOW_FOCUS_IN       = 3,
    EVENT_WINDOW_FOCUS_OUT      = 4,
    EVENT_WINDOW_MOVE           = 5,
    EVENT_WINDOW_RESIZE         = 6,
    EVENT_WINDOW_STATE_CHANGE   = 7,
    EVENT_WINDOW_EXPOSE         = 8,
    EVENT_KEY_PRESS             = 9,
    EVENT_KEY_RELEASE           = 10,
    EVENT_MOUSE_PRESS           = 11,
    EVENT_MOUSE_RELEASE         = 12,
    EVENT_MOUSE_MOVE            = 13,
    EVENT_MOUSE_ENTER           = 14,
    EVENT_MOUSE_LEAVE           = 15,
    EVENT_TOUCH_BEGIN           = 16,
    EVENT_TOUCH_UPDATE          = 17,
    EVENT_TOUCH_END             = 18,
} event_type_t;

/* Rectangle */
typedef struct {
    int32_t x, y;
    uint32_t width, height;
} rect_t;

/* Event */
typedef struct {
    event_type_t type;
    uint32_t window_id;
    uint64_t timestamp;
    
    union {
        struct {
            int32_t x, y;
            uint32_t width, height;
        } geometry;
        
        struct {
            uint32_t keycode;
            uint32_t modifiers;
            char character;
        } key;
        
        struct {
            int32_t x, y;
            int32_t delta_x, delta_y;
            uint32_t button;
            uint32_t modifiers;
        } mouse;
        
        struct {
            int32_t x, y;
            uint32_t id;
            float pressure;
        } touch;
        
        struct {
            window_state_t old_state;
            window_state_t new_state;
        } state;
    } data;
} window_event_t;

/* Window */
typedef struct window {
    uint32_t window_id;
    uint32_t owner_pid;
    
    char title[256];
    
    rect_t geometry;
    rect_t saved_geometry;
    
    window_state_t state;
    window_type_t type;
    uint32_t flags;
    
    uint32_t z_order;
    
    struct window *parent;
    struct window *children;
    uint32_t num_children;
    
    void *framebuffer;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_stride;
    
    bool needs_redraw;
    bool has_focus;
    
    uint64_t create_time;
    uint64_t last_update;
    
    /* Event queue for this window */
    window_event_t *event_queue;
    uint32_t event_queue_head;
    uint32_t event_queue_tail;
    uint32_t event_queue_size;
    
    struct window *next;
    struct window *prev;
    
} window_t;

/* Window server state */
typedef struct {
    window_t *windows;
    window_t *focused_window;
    window_t *root_window;
    
    uint32_t num_windows;
    uint32_t next_window_id;
    
    uint32_t screen_width;
    uint32_t screen_height;
    
    /* Decorations */
    uint32_t titlebar_height;
    uint32_t border_width;
    
    /* Cursor */
    int32_t cursor_x;
    int32_t cursor_y;
    bool cursor_visible;
    
    /* Drag state */
    bool dragging;
    bool resizing;
    window_t *drag_window;
    int32_t drag_start_x;
    int32_t drag_start_y;
    rect_t drag_start_geometry;
    
    /* Mode */
    visor_mode_t current_mode;
    
    /* Statistics */
    uint64_t total_events;
    uint64_t total_redraws;
    
    volatile uint32_t lock;
    
} window_server_t;

/* Global state */
static window_server_t g_wserver;

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

/* Utility functions */
static size_t ws_strlen(const char *s)
{
    size_t len = 0;
    while (*s++) len++;
    return len;
}

static char *ws_strcpy(char *dst, const char *src)
{
    char *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

static bool rect_contains(const rect_t *rect, int32_t x, int32_t y)
{
    return x >= rect->x && x < rect->x + (int32_t)rect->width &&
           y >= rect->y && y < rect->y + (int32_t)rect->height;
}

static bool rect_intersects(const rect_t *a, const rect_t *b)
{
    return !(a->x + (int32_t)a->width <= b->x ||
             b->x + (int32_t)b->width <= a->x ||
             a->y + (int32_t)a->height <= b->y ||
             b->y + (int32_t)b->height <= a->y);
}

/* Find window by ID */
static window_t *find_window(uint32_t window_id)
{
    window_t *win = g_wserver.windows;
    
    while (win) {
        if (win->window_id == window_id) {
            return win;
        }
        win = win->next;
    }
    
    return NULL;
}

/* Find window at position */
static window_t *find_window_at(int32_t x, int32_t y)
{
    window_t *win = g_wserver.windows;
    window_t *found = NULL;
    uint32_t highest_z = 0;
    
    /* Find topmost window at position */
    while (win) {
        if (win->state != WINDOW_STATE_MINIMIZED &&
            win->state != WINDOW_STATE_HIDDEN &&
            rect_contains(&win->geometry, x, y)) {
            
            if (!found || win->z_order > highest_z) {
                found = win;
                highest_z = win->z_order;
            }
        }
        win = win->next;
    }
    
    return found;
}

/* Queue event to window */
static void queue_event(window_t *win, window_event_t *event)
{
    if (!win || !event) return;
    
    uint32_t next_tail = (win->event_queue_tail + 1) % win->event_queue_size;
    
    if (next_tail == win->event_queue_head) {
        /* Queue full - drop oldest event */
        win->event_queue_head = (win->event_queue_head + 1) % win->event_queue_size;
    }
    
    memcpy(&win->event_queue[win->event_queue_tail], event, sizeof(window_event_t));
    win->event_queue_tail = next_tail;
    
    g_wserver.total_events++;
}

/* Initialize window server */
void window_server_init(void)
{
    uart_puts("[WINDOW_SERVER] Initializing window server\n");
    
    memset(&g_wserver, 0, sizeof(window_server_t));
    
    /* Get screen dimensions */
    extern void framebuffer_get_info(uint32_t, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
    framebuffer_get_info(0, &g_wserver.screen_width, &g_wserver.screen_height, NULL, NULL);
    
    if (g_wserver.screen_width == 0) {
        g_wserver.screen_width = 3840;
        g_wserver.screen_height = 2160;
    }
    
    g_wserver.next_window_id = 1;
    g_wserver.titlebar_height = 32;
    g_wserver.border_width = 2;
    g_wserver.cursor_visible = true;
    
    extern visor_mode_t mode_get_current(void);
    g_wserver.current_mode = mode_get_current();
    
    uart_puts("[WINDOW_SERVER] Screen: ");
    uart_put_dec(g_wserver.screen_width);
    uart_puts("x");
    uart_put_dec(g_wserver.screen_height);
    uart_puts("\n");
    
    uart_puts("[WINDOW_SERVER] Window server initialized\n");
}

/* Create window */
uint32_t window_create(uint32_t owner_pid, const char *title, int32_t x, int32_t y, 
                       uint32_t width, uint32_t height, window_type_t type, uint32_t flags)
{
    window_t *win;
    
    win = (window_t *)kalloc(sizeof(window_t));
    if (!win) return 0;
    
    memset(win, 0, sizeof(window_t));
    
    spinlock_acquire(&g_wserver.lock);
    
    win->window_id = g_wserver.next_window_id++;
    win->owner_pid = owner_pid;
    win->type = type;
    win->flags = flags;
    win->state = WINDOW_STATE_NORMAL;
    
    /* Copy title */
    if (title) {
        size_t len = ws_strlen(title);
        if (len >= sizeof(win->title)) len = sizeof(win->title) - 1;
        memcpy(win->title, title, len);
        win->title[len] = '\0';
    }
    
    /* Set geometry */
    win->geometry.x = x;
    win->geometry.y = y;
    win->geometry.width = width;
    win->geometry.height = height;
    
    /* Allocate framebuffer */
    win->fb_width = width;
    win->fb_height = height;
    win->fb_stride = width * 4;  /* RGBA */
    win->framebuffer = kalloc(win->fb_stride * win->fb_height);
    
    if (!win->framebuffer) {
        spinlock_release(&g_wserver.lock);
        kfree(win);
        return 0;
    }
    
    memset(win->framebuffer, 0xFF, win->fb_stride * win->fb_height);
    
    /* Allocate event queue */
    win->event_queue_size = 128;
    win->event_queue = (window_event_t *)kalloc(sizeof(window_event_t) * win->event_queue_size);
    
    if (!win->event_queue) {
        kfree(win->framebuffer);
        spinlock_release(&g_wserver.lock);
        kfree(win);
        return 0;
    }
    
    /* Set Z-order */
    win->z_order = g_wserver.num_windows;
    
    /* Set timestamps */
    win->create_time = get_timer_count();
    win->last_update = win->create_time;
    win->needs_redraw = true;
    
    /* Add to list */
    win->next = g_wserver.windows;
    if (g_wserver.windows) {
        g_wserver.windows->prev = win;
    }
    g_wserver.windows = win;
    g_wserver.num_windows++;
    
    spinlock_release(&g_wserver.lock);
    
    /* Send create event */
    window_event_t event;
    memset(&event, 0, sizeof(window_event_t));
    event.type = EVENT_WINDOW_CREATE;
    event.window_id = win->window_id;
    event.timestamp = get_timer_count();
    event.data.geometry.x = x;
    event.data.geometry.y = y;
    event.data.geometry.width = width;
    event.data.geometry.height = height;
    queue_event(win, &event);
    
    /* Notify compositor */
    extern uint32_t compositor_create_window(uint32_t, const char*, uint32_t, uint32_t);
    compositor_create_window(owner_pid, title, width, height);
    
    uart_puts("[WINDOW_SERVER] Created window ");
    uart_put_dec(win->window_id);
    uart_puts(": ");
    uart_puts(title ? title : "(untitled)");
    uart_puts("\n");
    
    return win->window_id;
}

/* Destroy window */
void window_destroy(uint32_t window_id)
{
    spinlock_acquire(&g_wserver.lock);
    
    window_t *win = find_window(window_id);
    if (!win) {
        spinlock_release(&g_wserver.lock);
        return;
    }
    
    uart_puts("[WINDOW_SERVER] Destroying window ");
    uart_put_dec(window_id);
    uart_puts("\n");
    
    /* Send destroy event */
    window_event_t event;
    memset(&event, 0, sizeof(window_event_t));
    event.type = EVENT_WINDOW_DESTROY;
    event.window_id = window_id;
    event.timestamp = get_timer_count();
    queue_event(win, &event);
    
    /* Remove from focus */
    if (g_wserver.focused_window == win) {
        g_wserver.focused_window = NULL;
    }
    
    /* Remove from list */
    if (win->prev) {
        win->prev->next = win->next;
    } else {
        g_wserver.windows = win->next;
    }
    
    if (win->next) {
        win->next->prev = win->prev;
    }
    
    g_wserver.num_windows--;
    
    /* Free resources */
    if (win->framebuffer) kfree(win->framebuffer);
    if (win->event_queue) kfree(win->event_queue);
    kfree(win);
    
    spinlock_release(&g_wserver.lock);
}

/* Set window title */
void window_set_title(uint32_t window_id, const char *title)
{
    spinlock_acquire(&g_wserver.lock);
    
    window_t *win = find_window(window_id);
    if (win && title) {
        size_t len = ws_strlen(title);
        if (len >= sizeof(win->title)) len = sizeof(win->title) - 1;
        memcpy(win->title, title, len);
        win->title[len] = '\0';
        win->needs_redraw = true;
    }
    
    spinlock_release(&g_wserver.lock);
}

/* Move window */
void window_move(uint32_t window_id, int32_t x, int32_t y)
{
    spinlock_acquire(&g_wserver.lock);
    
    window_t *win = find_window(window_id);
    if (!win) {
        spinlock_release(&g_wserver.lock);
        return;
    }
    
    if (!(win->flags & WINDOW_FLAG_MOVABLE)) {
        spinlock_release(&g_wserver.lock);
        return;
    }
    
    win->geometry.x = x;
    win->geometry.y = y;
    win->needs_redraw = true;
    
    /* Send move event */
    window_event_t event;
    memset(&event, 0, sizeof(window_event_t));
    event.type = EVENT_WINDOW_MOVE;
    event.window_id = window_id;
    event.timestamp = get_timer_count();
    event.data.geometry.x = x;
    event.data.geometry.y = y;
    queue_event(win, &event);
    
    spinlock_release(&g_wserver.lock);
}

/* Resize window */
void window_resize(uint32_t window_id, uint32_t width, uint32_t height)
{
    spinlock_acquire(&g_wserver.lock);
    
    window_t *win = find_window(window_id);
    if (!win) {
        spinlock_release(&g_wserver.lock);
        return;
    }
    
    if (!(win->flags & WINDOW_FLAG_RESIZABLE)) {
        spinlock_release(&g_wserver.lock);
        return;
    }
    
    /* Reallocate framebuffer if needed */
    uint32_t new_stride = width * 4;
    size_t new_size = new_stride * height;
    
    if (new_size != win->fb_stride * win->fb_height) {
        void *new_fb = kalloc(new_size);
        if (new_fb) {
            memset(new_fb, 0xFF, new_size);
            kfree(win->framebuffer);
            win->framebuffer = new_fb;
            win->fb_width = width;
            win->fb_height = height;
            win->fb_stride = new_stride;
        }
    }
    
    win->geometry.width = width;
    win->geometry.height = height;
    win->needs_redraw = true;
    
    /* Send resize event */
    window_event_t event;
    memset(&event, 0, sizeof(window_event_t));
    event.type = EVENT_WINDOW_RESIZE;
    event.window_id = window_id;
    event.timestamp = get_timer_count();
    event.data.geometry.width = width;
    event.data.geometry.height = height;
    queue_event(win, &event);
    
    spinlock_release(&g_wserver.lock);
}

/* Set window state */
void window_set_state(uint32_t window_id, window_state_t state)
{
    spinlock_acquire(&g_wserver.lock);
    
    window_t *win = find_window(window_id);
    if (!win) {
        spinlock_release(&g_wserver.lock);
        return;
    }
    
    window_state_t old_state = win->state;
    
    /* Save geometry before state change */
    if (state == WINDOW_STATE_MAXIMIZED || state == WINDOW_STATE_FULLSCREEN) {
        win->saved_geometry = win->geometry;
    }
    
    /* Apply state */
    if (state == WINDOW_STATE_MAXIMIZED) {
        win->geometry.x = 0;
        win->geometry.y = g_wserver.titlebar_height;
        win->geometry.width = g_wserver.screen_width;
        win->geometry.height = g_wserver.screen_height - g_wserver.titlebar_height;
    } else if (state == WINDOW_STATE_FULLSCREEN) {
        win->geometry.x = 0;
        win->geometry.y = 0;
        win->geometry.width = g_wserver.screen_width;
        win->geometry.height = g_wserver.screen_height;
    } else if (state == WINDOW_STATE_NORMAL && 
               (old_state == WINDOW_STATE_MAXIMIZED || old_state == WINDOW_STATE_FULLSCREEN)) {
        /* Restore saved geometry */
        win->geometry = win->saved_geometry;
    }
    
    win->state = state;
    win->needs_redraw = true;
    
    /* Send state change event */
    window_event_t event;
    memset(&event, 0, sizeof(window_event_t));
    event.type = EVENT_WINDOW_STATE_CHANGE;
    event.window_id = window_id;
    event.timestamp = get_timer_count();
    event.data.state.old_state = old_state;
    event.data.state.new_state = state;
    queue_event(win, &event);
    
    spinlock_release(&g_wserver.lock);
}

/* Focus window */
void window_focus(uint32_t window_id)
{
    spinlock_acquire(&g_wserver.lock);
    
    window_t *win = find_window(window_id);
    if (!win) {
        spinlock_release(&g_wserver.lock);
        return;
    }
    
    /* Remove focus from previous window */
    if (g_wserver.focused_window && g_wserver.focused_window != win) {
        g_wserver.focused_window->has_focus = false;
        
        window_event_t event;
        memset(&event, 0, sizeof(window_event_t));
        event.type = EVENT_WINDOW_FOCUS_OUT;
        event.window_id = g_wserver.focused_window->window_id;
        event.timestamp = get_timer_count();
        queue_event(g_wserver.focused_window, &event);
    }
    
    /* Set new focus */
    g_wserver.focused_window = win;
    win->has_focus = true;
    win->needs_redraw = true;
    
    /* Bring to front */
    win->z_order = g_wserver.num_windows;
    
    /* Send focus event */
    window_event_t event;
    memset(&event, 0, sizeof(window_event_t));
    event.type = EVENT_WINDOW_FOCUS_IN;
    event.window_id = window_id;
    event.timestamp = get_timer_count();
    queue_event(win, &event);
    
    spinlock_release(&g_wserver.lock);
}

/* Handle mouse event */
void window_server_handle_mouse(int32_t x, int32_t y, uint32_t button, bool pressed)
{
    g_wserver.cursor_x = x;
    g_wserver.cursor_y = y;
    
    spinlock_acquire(&g_wserver.lock);
    
    window_t *win = find_window_at(x, y);
    
    if (pressed && win) {
        /* Focus window on click */
        if (!win->has_focus) {
            spinlock_release(&g_wserver.lock);
            window_focus(win->window_id);
            spinlock_acquire(&g_wserver.lock);
        }
        
        /* Check if clicking on titlebar (for dragging) */
        rect_t titlebar = win->geometry;
        titlebar.height = g_wserver.titlebar_height;
        
        if (rect_contains(&titlebar, x, y) && (win->flags & WINDOW_FLAG_MOVABLE)) {
            g_wserver.dragging = true;
            g_wserver.drag_window = win;
            g_wserver.drag_start_x = x;
            g_wserver.drag_start_y = y;
            g_wserver.drag_start_geometry = win->geometry;
        }
    } else if (!pressed) {
        g_wserver.dragging = false;
        g_wserver.resizing = false;
        g_wserver.drag_window = NULL;
    }
    
    /* Send mouse event to window */
    if (win) {
        window_event_t event;
        memset(&event, 0, sizeof(window_event_t));
        event.type = pressed ? EVENT_MOUSE_PRESS : EVENT_MOUSE_RELEASE;
        event.window_id = win->window_id;
        event.timestamp = get_timer_count();
        event.data.mouse.x = x - win->geometry.x;
        event.data.mouse.y = y - win->geometry.y;
        event.data.mouse.button = button;
        queue_event(win, &event);
    }
    
    spinlock_release(&g_wserver.lock);
}

/* Handle mouse move */
void window_server_handle_mouse_move(int32_t x, int32_t y)
{
    int32_t old_x = g_wserver.cursor_x;
    int32_t old_y = g_wserver.cursor_y;
    
    g_wserver.cursor_x = x;
    g_wserver.cursor_y = y;
    
    spinlock_acquire(&g_wserver.lock);
    
    /* Handle dragging */
    if (g_wserver.dragging && g_wserver.drag_window) {
        int32_t delta_x = x - g_wserver.drag_start_x;
        int32_t delta_y = y - g_wserver.drag_start_y;
        
        g_wserver.drag_window->geometry.x = g_wserver.drag_start_geometry.x + delta_x;
        g_wserver.drag_window->geometry.y = g_wserver.drag_start_geometry.y + delta_y;
        g_wserver.drag_window->needs_redraw = true;
    }
    
    /* Find window under cursor */
    window_t *win = find_window_at(x, y);
    
    /* Send mouse move event */
    if (win) {
        window_event_t event;
        memset(&event, 0, sizeof(window_event_t));
        event.type = EVENT_MOUSE_MOVE;
        event.window_id = win->window_id;
        event.timestamp = get_timer_count();
        event.data.mouse.x = x - win->geometry.x;
        event.data.mouse.y = y - win->geometry.y;
        event.data.mouse.delta_x = x - old_x;
        event.data.mouse.delta_y = y - old_y;
        queue_event(win, &event);
    }
    
    spinlock_release(&g_wserver.lock);
}

/* Poll event from window */
bool window_poll_event(uint32_t window_id, window_event_t *event)
{
    spinlock_acquire(&g_wserver.lock);
    
    window_t *win = find_window(window_id);
    if (!win || !event) {
        spinlock_release(&g_wserver.lock);
        return false;
    }
    
    if (win->event_queue_head == win->event_queue_tail) {
        /* Queue empty */
        spinlock_release(&g_wserver.lock);
        return false;
    }
    
    /* Dequeue event */
    memcpy(event, &win->event_queue[win->event_queue_head], sizeof(window_event_t));
    win->event_queue_head = (win->event_queue_head + 1) % win->event_queue_size;
    
    spinlock_release(&g_wserver.lock);
    return true;
}

/* Get window framebuffer */
void *window_get_framebuffer(uint32_t window_id)
{
    window_t *win = find_window(window_id);
    return win ? win->framebuffer : NULL;
}

/* Mark window for redraw */
void window_mark_dirty(uint32_t window_id)
{
    spinlock_acquire(&g_wserver.lock);
    
    window_t *win = find_window(window_id);
    if (win) {
        win->needs_redraw = true;
        g_wserver.total_redraws++;
    }
    
    spinlock_release(&g_wserver.lock);
}

/* Reconfigure for mode change */
void window_server_reconfigure(visor_mode_t mode)
{
    spinlock_acquire(&g_wserver.lock);
    
    g_wserver.current_mode = mode;
    
    /* Adjust decorations based on mode */
    switch (mode) {
        case 0:  /* Phone */
            g_wserver.titlebar_height = 0;
            g_wserver.border_width = 0;
            break;
        case 1:  /* Tablet */
            g_wserver.titlebar_height = 40;
            g_wserver.border_width = 2;
            break;
        case 2:  /* Laptop */
            g_wserver.titlebar_height = 32;
            g_wserver.border_width = 2;
            break;
        case 3:  /* TV */
            g_wserver.titlebar_height = 0;
            g_wserver.border_width = 0;
            break;
    }
    
    /* Mark all windows for redraw */
    window_t *win = g_wserver.windows;
    while (win) {
        win->needs_redraw = true;
        win = win->next;
    }
    
    spinlock_release(&g_wserver.lock);
}

/* Get server statistics */
void window_server_print_stats(void)
{
    uart_puts("\n[WINDOW_SERVER] Statistics:\n");
    uart_puts("════════════════════════════════════════\n");
    
    uart_puts("  Total windows:     ");
    uart_put_dec(g_wserver.num_windows);
    uart_puts("\n");
    
    uart_puts("  Total events:      ");
    uart_put_dec(g_wserver.total_events);
    uart_puts("\n");
    
    uart_puts("  Total redraws:     ");
    uart_put_dec(g_wserver.total_redraws);
    uart_puts("\n");
    
    uart_puts("  Screen size:       ");
    uart_put_dec(g_wserver.screen_width);
    uart_puts("x");
    uart_put_dec(g_wserver.screen_height);
    uart_puts("\n");
    
    if (g_wserver.focused_window) {
        uart_puts("  Focused window:    ");
        uart_put_dec(g_wserver.focused_window->window_id);
        uart_puts(" (");
        uart_puts(g_wserver.focused_window->title);
        uart_puts(")\n");
    }
    
    uart_puts("\n");
}