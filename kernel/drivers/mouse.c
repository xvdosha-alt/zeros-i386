#include "mouse.h"
#include "io.h"

static MouseState g_mouse;
static int g_packet[4];
static int g_cycle;
static int g_packet_size = 3;
static int g_have_wheel;
static int g_wheel;
static int g_moved;
static int g_screen_w = 1024;
static int g_screen_h = 768;

static void mouse_wait(int type) {
    int timeout = 100000;
    if (type == 0) {
        while (timeout-- && !(inb(0x64) & 1)) {}
    } else {
        while (timeout-- && (inb(0x64) & 2)) {}
    }
}

static void mouse_write(uint8_t val) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, val);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

static void mouse_enable_wheel(void) {
    mouse_write(0xF3);
    (void)mouse_read();
    mouse_write(200);
    (void)mouse_read();
    mouse_write(0xF3);
    (void)mouse_read();
    mouse_write(100);
    (void)mouse_read();
    mouse_write(0xF3);
    (void)mouse_read();
    mouse_write(80);
    (void)mouse_read();
    mouse_write(0xF2);
    if (mouse_read() == 3) {
        g_have_wheel = 1;
        g_packet_size = 4;
    }
}

static void mouse_handle_byte(uint8_t data) {
    int flags, dx, dy;

    if (g_cycle == 0 && (data & 0x08) == 0)
        return;

    g_packet[g_cycle++] = (int)data;
    if (g_cycle < g_packet_size)
        return;
    g_cycle = 0;

    flags = g_packet[0];
    if ((flags & 0x08) == 0)
        return;
    if (flags & 0xC0)
        return;

    dx = (int)(uint8_t)g_packet[1];
    dy = (int)(uint8_t)g_packet[2];
    if (flags & 0x10)
        dx -= 256;
    if (flags & 0x20)
        dy -= 256;

    dy = -dy;

    g_mouse.dx = dx;
    g_mouse.dy = dy;
    g_mouse.x += dx;
    g_mouse.y += dy;
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.x >= g_screen_w) g_mouse.x = g_screen_w - 1;
    if (g_mouse.y >= g_screen_h) g_mouse.y = g_screen_h - 1;
    g_mouse.buttons = flags & 0x07;

    if (g_have_wheel && g_packet_size >= 4) {
        int z = (int8_t)(uint8_t)g_packet[3];
        if (z)
            g_wheel += z;
    }

    g_moved = 1;
}

void mouse_init(void) {
    g_mouse.x = 512;
    g_mouse.y = 384;
    g_mouse.buttons = 0;
    g_cycle = 0;
    g_moved = 0;
    g_wheel = 0;
    g_have_wheel = 0;
    g_packet_size = 3;

    mouse_wait(1);
    outb(0x64, 0xA8);

    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    {
        uint8_t status = inb(0x60);
        status |= 0x02;
        status &= (uint8_t)~0x20;
        mouse_wait(1);
        outb(0x64, 0x60);
        mouse_wait(1);
        outb(0x60, status);
    }

    mouse_write(0xF6);
    (void)mouse_read();
    mouse_enable_wheel();
    mouse_write(0xF4);
    (void)mouse_read();
}

void mouse_irq_handler(void) {
    uint8_t status = inb(0x64);
    if (!(status & 1)) return;
    if (!(status & 0x20)) return;
    mouse_handle_byte(inb(0x60));
}

void mouse_byte(uint8_t data) {
    mouse_handle_byte(data);
}

void mouse_poll(void) {
    int guard = 64;
    while (guard-- > 0) {
        uint8_t status = inb(0x64);
        if (!(status & 1)) break;
        if (status & 0x20)
            mouse_handle_byte(inb(0x60));
        else
            break;
    }
}

void mouse_state(MouseState *out) {
    if (!out) return;
    out->x = g_mouse.x;
    out->y = g_mouse.y;
    out->buttons = g_mouse.buttons;
    out->dx = g_mouse.dx;
    out->dy = g_mouse.dy;
}

void mouse_coords(int *x, int *y, int *buttons) {
    if (x) *x = g_mouse.x;
    if (y) *y = g_mouse.y;
    if (buttons) {
        int b = g_mouse.buttons;
        if (g_wheel > 0) {
            b |= 0x10;
            g_wheel--;
        } else if (g_wheel < 0) {
            b |= 0x20;
            g_wheel++;
        }
        *buttons = b;
    }
}

int mouse_moved(void) {
    int m = g_moved;
    g_moved = 0;
    return m || g_wheel != 0;
}

void mouse_set_bounds(int w, int h) {
    if (w > 0) g_screen_w = w;
    if (h > 0) g_screen_h = h;
    if (g_mouse.x >= g_screen_w) g_mouse.x = g_screen_w - 1;
    if (g_mouse.y >= g_screen_h) g_mouse.y = g_screen_h - 1;
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
}

void mouse_enable_stream(void) {
    mouse_write(0xF4);
    (void)mouse_read();
}
