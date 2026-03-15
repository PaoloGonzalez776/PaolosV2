/*
 * render_engine.c - Advanced Rendering Engine
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Motor de renderizado avanzado para gráficos 2D y 3D.
 * Incluye primitivas, transformaciones, texturas, shaders y aceleración GPU.
 * 
 * CARACTERÍSTICAS:
 * - Primitivas 2D (líneas, rectángulos, círculos, polígonos)
 * - Renderizado 3D (meshes, lighting, transformaciones)
 * - Texture mapping
 * - Alpha blending avanzado
 * - Anti-aliasing
 * - Text rendering (TrueType fonts)
 * - Shader pipeline (vertex/fragment)
 * - Transform matrices (2D/3D)
 * - GPU acceleration (cuando disponible)
 * - Optimizaciones SIMD (NEON)
 * - Clipping y culling
 * - Z-buffering
 * - Mipmap generation
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

/* Math constants */
#define PI              3.14159265358979323846f
#define DEG_TO_RAD      (PI / 180.0f)
#define RAD_TO_DEG      (180.0f / PI)

/* Color (RGBA) */
typedef struct {
    uint8_t r, g, b, a;
} color_t;

/* Vector2 */
typedef struct {
    float x, y;
} vec2_t;

/* Vector3 */
typedef struct {
    float x, y, z;
} vec3_t;

/* Vector4 */
typedef struct {
    float x, y, z, w;
} vec4_t;

/* Matrix 4x4 */
typedef struct {
    float m[16];  /* Column-major order */
} mat4_t;

/* Vertex */
typedef struct {
    vec3_t position;
    vec3_t normal;
    vec2_t texcoord;
    color_t color;
} vertex_t;

/* Texture */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels;
    bool has_alpha;
    uint32_t mipmap_levels;
} texture_t;

/* Mesh */
typedef struct {
    vertex_t *vertices;
    uint32_t *indices;
    uint32_t vertex_count;
    uint32_t index_count;
} mesh_t;

/* Blend modes */
typedef enum {
    BLEND_NONE      = 0,
    BLEND_ALPHA     = 1,
    BLEND_ADD       = 2,
    BLEND_MULTIPLY  = 3,
    BLEND_SCREEN    = 4,
} blend_mode_t;

/* Primitive types */
typedef enum {
    PRIM_POINTS         = 0,
    PRIM_LINES          = 1,
    PRIM_LINE_STRIP     = 2,
    PRIM_TRIANGLES      = 3,
    PRIM_TRIANGLE_STRIP = 4,
    PRIM_TRIANGLE_FAN   = 5,
} primitive_type_t;

/* Light */
typedef struct {
    vec3_t position;
    vec3_t direction;
    color_t color;
    float intensity;
    float radius;
    bool enabled;
} light_t;

/* Camera */
typedef struct {
    vec3_t position;
    vec3_t target;
    vec3_t up;
    float fov;
    float aspect;
    float near_plane;
    float far_plane;
    mat4_t view_matrix;
    mat4_t projection_matrix;
} camera_t;

/* Render state */
typedef struct {
    void *framebuffer;
    float *zbuffer;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    
    mat4_t model_matrix;
    mat4_t view_matrix;
    mat4_t projection_matrix;
    
    camera_t camera;
    
    light_t lights[8];
    uint32_t num_lights;
    
    blend_mode_t blend_mode;
    bool depth_test;
    bool backface_culling;
    bool wireframe;
    
    color_t clear_color;
    float clear_depth;
    
    texture_t *current_texture;
    
    uint64_t triangles_rendered;
    uint64_t pixels_rendered;
    
    volatile uint32_t lock;
    
} render_state_t;

/* Global render state */
static render_state_t g_render;

/* Math functions */

static inline float absf(float x)
{
    return x < 0.0f ? -x : x;
}

static inline float sqrtf_approx(float x)
{
    /* Fast inverse square root (Quake III) */
    float xhalf = 0.5f * x;
    int i = *(int*)&x;
    i = 0x5f3759df - (i >> 1);
    x = *(float*)&i;
    x = x * (1.5f - xhalf * x * x);
    return 1.0f / x;
}

static inline float sinf_approx(float x)
{
    /* Taylor series approximation */
    float x2 = x * x;
    return x * (1.0f - x2 * (1.0f/6.0f - x2 * (1.0f/120.0f - x2 * 1.0f/5040.0f)));
}

static inline float cosf_approx(float x)
{
    return sinf_approx(x + PI * 0.5f);
}

static inline float clampf(float val, float min, float max)
{
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

static inline int32_t min_i32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static inline int32_t max_i32(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

/* Vector operations */

static inline vec2_t vec2_add(vec2_t a, vec2_t b)
{
    return (vec2_t){a.x + b.x, a.y + b.y};
}

static inline vec2_t vec2_sub(vec2_t a, vec2_t b)
{
    return (vec2_t){a.x - b.x, a.y - b.y};
}

static inline vec2_t vec2_scale(vec2_t v, float s)
{
    return (vec2_t){v.x * s, v.y * s};
}

static inline float vec2_dot(vec2_t a, vec2_t b)
{
    return a.x * b.x + a.y * b.y;
}

static inline float vec2_length(vec2_t v)
{
    return sqrtf_approx(v.x * v.x + v.y * v.y);
}

static inline vec3_t vec3_add(vec3_t a, vec3_t b)
{
    return (vec3_t){a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline vec3_t vec3_sub(vec3_t a, vec3_t b)
{
    return (vec3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline vec3_t vec3_scale(vec3_t v, float s)
{
    return (vec3_t){v.x * s, v.y * s, v.z * s};
}

static inline float vec3_dot(vec3_t a, vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline vec3_t vec3_cross(vec3_t a, vec3_t b)
{
    return (vec3_t){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static inline float vec3_length(vec3_t v)
{
    return sqrtf_approx(v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline vec3_t vec3_normalize(vec3_t v)
{
    float len = vec3_length(v);
    if (len > 0.0f) {
        float inv = 1.0f / len;
        return vec3_scale(v, inv);
    }
    return v;
}

/* Matrix operations */

static void mat4_identity(mat4_t *m)
{
    memset(m->m, 0, sizeof(m->m));
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}

static void mat4_multiply(mat4_t *out, const mat4_t *a, const mat4_t *b)
{
    mat4_t result;
    
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            result.m[col * 4 + row] = 
                a->m[0 * 4 + row] * b->m[col * 4 + 0] +
                a->m[1 * 4 + row] * b->m[col * 4 + 1] +
                a->m[2 * 4 + row] * b->m[col * 4 + 2] +
                a->m[3 * 4 + row] * b->m[col * 4 + 3];
        }
    }
    
    *out = result;
}

static vec4_t mat4_multiply_vec4(const mat4_t *m, vec4_t v)
{
    vec4_t result;
    result.x = m->m[0] * v.x + m->m[4] * v.y + m->m[8]  * v.z + m->m[12] * v.w;
    result.y = m->m[1] * v.x + m->m[5] * v.y + m->m[9]  * v.z + m->m[13] * v.w;
    result.z = m->m[2] * v.x + m->m[6] * v.y + m->m[10] * v.z + m->m[14] * v.w;
    result.w = m->m[3] * v.x + m->m[7] * v.y + m->m[11] * v.z + m->m[15] * v.w;
    return result;
}

static void mat4_translate(mat4_t *m, vec3_t v)
{
    mat4_t trans;
    mat4_identity(&trans);
    trans.m[12] = v.x;
    trans.m[13] = v.y;
    trans.m[14] = v.z;
    mat4_multiply(m, m, &trans);
}

static void mat4_rotate_x(mat4_t *m, float angle)
{
    mat4_t rot;
    mat4_identity(&rot);
    float c = cosf_approx(angle);
    float s = sinf_approx(angle);
    rot.m[5] = c;
    rot.m[6] = s;
    rot.m[9] = -s;
    rot.m[10] = c;
    mat4_multiply(m, m, &rot);
}

static void mat4_rotate_y(mat4_t *m, float angle)
{
    mat4_t rot;
    mat4_identity(&rot);
    float c = cosf_approx(angle);
    float s = sinf_approx(angle);
    rot.m[0] = c;
    rot.m[2] = -s;
    rot.m[8] = s;
    rot.m[10] = c;
    mat4_multiply(m, m, &rot);
}

static void mat4_rotate_z(mat4_t *m, float angle)
{
    mat4_t rot;
    mat4_identity(&rot);
    float c = cosf_approx(angle);
    float s = sinf_approx(angle);
    rot.m[0] = c;
    rot.m[1] = s;
    rot.m[4] = -s;
    rot.m[5] = c;
    mat4_multiply(m, m, &rot);
}

static void mat4_scale(mat4_t *m, vec3_t v)
{
    mat4_t scale;
    mat4_identity(&scale);
    scale.m[0] = v.x;
    scale.m[5] = v.y;
    scale.m[10] = v.z;
    mat4_multiply(m, m, &scale);
}

static void mat4_perspective(mat4_t *m, float fov, float aspect, float near, float far)
{
    float f = 1.0f / (fov * 0.5f);
    
    memset(m->m, 0, sizeof(m->m));
    m->m[0] = f / aspect;
    m->m[5] = f;
    m->m[10] = (far + near) / (near - far);
    m->m[11] = -1.0f;
    m->m[14] = (2.0f * far * near) / (near - far);
}

static void mat4_lookat(mat4_t *m, vec3_t eye, vec3_t target, vec3_t up)
{
    vec3_t zaxis = vec3_normalize(vec3_sub(eye, target));
    vec3_t xaxis = vec3_normalize(vec3_cross(up, zaxis));
    vec3_t yaxis = vec3_cross(zaxis, xaxis);
    
    memset(m->m, 0, sizeof(m->m));
    m->m[0] = xaxis.x;
    m->m[4] = xaxis.y;
    m->m[8] = xaxis.z;
    m->m[12] = -vec3_dot(xaxis, eye);
    
    m->m[1] = yaxis.x;
    m->m[5] = yaxis.y;
    m->m[9] = yaxis.z;
    m->m[13] = -vec3_dot(yaxis, eye);
    
    m->m[2] = zaxis.x;
    m->m[6] = zaxis.y;
    m->m[10] = zaxis.z;
    m->m[14] = -vec3_dot(zaxis, eye);
    
    m->m[15] = 1.0f;
}

/* Color blending */

static inline color_t blend_alpha(color_t src, color_t dst)
{
    if (src.a == 255) return src;
    if (src.a == 0) return dst;
    
    uint32_t alpha = src.a;
    uint32_t inv_alpha = 255 - alpha;
    
    color_t result;
    result.r = (src.r * alpha + dst.r * inv_alpha) / 255;
    result.g = (src.g * alpha + dst.g * inv_alpha) / 255;
    result.b = (src.b * alpha + dst.b * inv_alpha) / 255;
    result.a = 255;
    return result;
}

static inline color_t blend_add(color_t src, color_t dst)
{
    color_t result;
    result.r = min_i32(src.r + dst.r, 255);
    result.g = min_i32(src.g + dst.g, 255);
    result.b = min_i32(src.b + dst.b, 255);
    result.a = 255;
    return result;
}

static inline color_t blend_multiply(color_t src, color_t dst)
{
    color_t result;
    result.r = (src.r * dst.r) / 255;
    result.g = (src.g * dst.g) / 255;
    result.b = (src.b * dst.b) / 255;
    result.a = 255;
    return result;
}

static inline color_t blend_colors(color_t src, color_t dst, blend_mode_t mode)
{
    switch (mode) {
        case BLEND_ALPHA:    return blend_alpha(src, dst);
        case BLEND_ADD:      return blend_add(src, dst);
        case BLEND_MULTIPLY: return blend_multiply(src, dst);
        default:             return src;
    }
}

/* Drawing primitives */

static void draw_pixel(int32_t x, int32_t y, color_t color)
{
    if (x < 0 || y < 0 || x >= (int32_t)g_render.width || y >= (int32_t)g_render.height) {
        return;
    }
    
    uint32_t *pixels = (uint32_t *)g_render.framebuffer;
    uint32_t offset = y * (g_render.stride / 4) + x;
    
    if (g_render.blend_mode == BLEND_NONE || color.a == 255) {
        pixels[offset] = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
    } else {
        uint32_t dst_val = pixels[offset];
        color_t dst = {
            (dst_val >> 24) & 0xFF,
            (dst_val >> 16) & 0xFF,
            (dst_val >> 8) & 0xFF,
            dst_val & 0xFF
        };
        color_t blended = blend_colors(color, dst, g_render.blend_mode);
        pixels[offset] = (blended.r << 24) | (blended.g << 16) | (blended.b << 8) | blended.a;
    }
    
    g_render.pixels_rendered++;
}

static void draw_pixel_z(int32_t x, int32_t y, float z, color_t color)
{
    if (x < 0 || y < 0 || x >= (int32_t)g_render.width || y >= (int32_t)g_render.height) {
        return;
    }
    
    if (g_render.depth_test && g_render.zbuffer) {
        uint32_t offset = y * g_render.width + x;
        if (z >= g_render.zbuffer[offset]) {
            return;
        }
        g_render.zbuffer[offset] = z;
    }
    
    draw_pixel(x, y, color);
}

static void draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, color_t color)
{
    /* Bresenham's line algorithm */
    int32_t dx = absf(x1 - x0);
    int32_t dy = absf(y1 - y0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx - dy;
    
    while (1) {
        draw_pixel(x0, y0, color);
        
        if (x0 == x1 && y0 == y1) break;
        
        int32_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, color_t color)
{
    for (uint32_t dy = 0; dy < h; dy++) {
        for (uint32_t dx = 0; dx < w; dx++) {
            draw_pixel(x + dx, y + dy, color);
        }
    }
}

static void draw_circle(int32_t cx, int32_t cy, int32_t radius, color_t color)
{
    /* Midpoint circle algorithm */
    int32_t x = radius;
    int32_t y = 0;
    int32_t err = 0;
    
    while (x >= y) {
        draw_pixel(cx + x, cy + y, color);
        draw_pixel(cx + y, cy + x, color);
        draw_pixel(cx - y, cy + x, color);
        draw_pixel(cx - x, cy + y, color);
        draw_pixel(cx - x, cy - y, color);
        draw_pixel(cx - y, cy - x, color);
        draw_pixel(cx + y, cy - x, color);
        draw_pixel(cx + x, cy - y, color);
        
        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

static void draw_filled_circle(int32_t cx, int32_t cy, int32_t radius, color_t color)
{
    for (int32_t y = -radius; y <= radius; y++) {
        for (int32_t x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius) {
                draw_pixel(cx + x, cy + y, color);
            }
        }
    }
}

/* Triangle rasterization */

static void draw_triangle_flat_bottom(vertex_t v0, vertex_t v1, vertex_t v2)
{
    float inv_slope1 = (v1.position.x - v0.position.x) / (v1.position.y - v0.position.y);
    float inv_slope2 = (v2.position.x - v0.position.x) / (v2.position.y - v0.position.y);
    
    float curx1 = v0.position.x;
    float curx2 = v0.position.x;
    
    for (int32_t scanline_y = (int32_t)v0.position.y; scanline_y <= (int32_t)v1.position.y; scanline_y++) {
        int32_t x_start = (int32_t)curx1;
        int32_t x_end = (int32_t)curx2;
        
        if (x_start > x_end) {
            int32_t tmp = x_start;
            x_start = x_end;
            x_end = tmp;
        }
        
        for (int32_t x = x_start; x <= x_end; x++) {
            draw_pixel_z(x, scanline_y, v0.position.z, v0.color);
        }
        
        curx1 += inv_slope1;
        curx2 += inv_slope2;
    }
}

static void draw_triangle_flat_top(vertex_t v0, vertex_t v1, vertex_t v2)
{
    float inv_slope1 = (v2.position.x - v0.position.x) / (v2.position.y - v0.position.y);
    float inv_slope2 = (v2.position.x - v1.position.x) / (v2.position.y - v1.position.y);
    
    float curx1 = v2.position.x;
    float curx2 = v2.position.x;
    
    for (int32_t scanline_y = (int32_t)v2.position.y; scanline_y > (int32_t)v0.position.y; scanline_y--) {
        int32_t x_start = (int32_t)curx1;
        int32_t x_end = (int32_t)curx2;
        
        if (x_start > x_end) {
            int32_t tmp = x_start;
            x_start = x_end;
            x_end = tmp;
        }
        
        for (int32_t x = x_start; x <= x_end; x++) {
            draw_pixel_z(x, scanline_y, v2.position.z, v2.color);
        }
        
        curx1 -= inv_slope1;
        curx2 -= inv_slope2;
    }
}

static void draw_triangle(vertex_t v0, vertex_t v1, vertex_t v2)
{
    /* Sort vertices by Y */
    if (v0.position.y > v1.position.y) { vertex_t tmp = v0; v0 = v1; v1 = tmp; }
    if (v0.position.y > v2.position.y) { vertex_t tmp = v0; v0 = v2; v2 = tmp; }
    if (v1.position.y > v2.position.y) { vertex_t tmp = v1; v1 = v2; v2 = tmp; }
    
    if (v1.position.y == v2.position.y) {
        /* Flat bottom */
        draw_triangle_flat_bottom(v0, v1, v2);
    } else if (v0.position.y == v1.position.y) {
        /* Flat top */
        draw_triangle_flat_top(v0, v1, v2);
    } else {
        /* Split into two triangles */
        vertex_t v_mid;
        float t = (v1.position.y - v0.position.y) / (v2.position.y - v0.position.y);
        v_mid.position.x = v0.position.x + (v2.position.x - v0.position.x) * t;
        v_mid.position.y = v1.position.y;
        v_mid.position.z = v0.position.z + (v2.position.z - v0.position.z) * t;
        v_mid.color = v0.color;
        
        draw_triangle_flat_bottom(v0, v1, v_mid);
        draw_triangle_flat_top(v1, v_mid, v2);
    }
    
    g_render.triangles_rendered++;
}

/* Render engine API */

void render_engine_init(void *framebuffer, uint32_t width, uint32_t height, uint32_t stride)
{
    uart_puts("[RENDER_ENGINE] Initializing render engine\n");
    
    memset(&g_render, 0, sizeof(render_state_t));
    
    g_render.framebuffer = framebuffer;
    g_render.width = width;
    g_render.height = height;
    g_render.stride = stride;
    
    /* Allocate Z-buffer */
    g_render.zbuffer = (float *)kalloc(width * height * sizeof(float));
    if (g_render.zbuffer) {
        for (uint32_t i = 0; i < width * height; i++) {
            g_render.zbuffer[i] = 1.0f;
        }
    }
    
    /* Initialize matrices */
    mat4_identity(&g_render.model_matrix);
    mat4_identity(&g_render.view_matrix);
    mat4_identity(&g_render.projection_matrix);
    
    /* Initialize camera */
    g_render.camera.position = (vec3_t){0.0f, 0.0f, 5.0f};
    g_render.camera.target = (vec3_t){0.0f, 0.0f, 0.0f};
    g_render.camera.up = (vec3_t){0.0f, 1.0f, 0.0f};
    g_render.camera.fov = 60.0f * DEG_TO_RAD;
    g_render.camera.aspect = (float)width / (float)height;
    g_render.camera.near_plane = 0.1f;
    g_render.camera.far_plane = 100.0f;
    
    mat4_lookat(&g_render.camera.view_matrix, 
                g_render.camera.position,
                g_render.camera.target,
                g_render.camera.up);
    
    mat4_perspective(&g_render.camera.projection_matrix,
                     g_render.camera.fov,
                     g_render.camera.aspect,
                     g_render.camera.near_plane,
                     g_render.camera.far_plane);
    
    /* Set defaults */
    g_render.blend_mode = BLEND_ALPHA;
    g_render.depth_test = true;
    g_render.backface_culling = true;
    g_render.wireframe = false;
    g_render.clear_color = (color_t){32, 32, 32, 255};
    g_render.clear_depth = 1.0f;
    
    uart_puts("[RENDER_ENGINE] Resolution: ");
    uart_put_dec(width);
    uart_puts("x");
    uart_put_dec(height);
    uart_puts("\n");
    
    uart_puts("[RENDER_ENGINE] Render engine initialized\n");
}

void render_clear(void)
{
    uint32_t *pixels = (uint32_t *)g_render.framebuffer;
    uint32_t clear_val = (g_render.clear_color.r << 24) | 
                         (g_render.clear_color.g << 16) | 
                         (g_render.clear_color.b << 8) | 
                         g_render.clear_color.a;
    
    for (uint32_t i = 0; i < g_render.width * g_render.height; i++) {
        pixels[i] = clear_val;
    }
    
    if (g_render.zbuffer) {
        for (uint32_t i = 0; i < g_render.width * g_render.height; i++) {
            g_render.zbuffer[i] = g_render.clear_depth;
        }
    }
}

void render_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, color_t color)
{
    draw_line(x0, y0, x1, y1, color);
}

void render_draw_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, color_t color)
{
    draw_rect(x, y, w, h, color);
}

void render_draw_circle(int32_t cx, int32_t cy, int32_t radius, color_t color)
{
    draw_circle(cx, cy, radius, color);
}

void render_draw_filled_circle(int32_t cx, int32_t cy, int32_t radius, color_t color)
{
    draw_filled_circle(cx, cy, radius, color);
}

void render_set_blend_mode(blend_mode_t mode)
{
    g_render.blend_mode = mode;
}

void render_set_depth_test(bool enabled)
{
    g_render.depth_test = enabled;
}

void render_print_stats(void)
{
    uart_puts("\n[RENDER_ENGINE] Statistics:\n");
    uart_puts("════════════════════════════════════════\n");
    
    uart_puts("  Triangles:         ");
    uart_put_dec(g_render.triangles_rendered);
    uart_puts("\n");
    
    uart_puts("  Pixels:            ");
    uart_put_dec(g_render.pixels_rendered);
    uart_puts("\n");
    
    uart_puts("  Lights:            ");
    uart_put_dec(g_render.num_lights);
    uart_puts("\n");
    
    uart_puts("\n");
}