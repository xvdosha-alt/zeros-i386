#include "kbd.h"
#include "io.h"
#include "mouse.h"

#define BUF_SIZE 128

static volatile int buf[BUF_SIZE];
static volatile uint8_t head;
static volatile uint8_t tail;
static volatile uint8_t shift;
static volatile uint8_t ctrl;
static volatile uint8_t alt;
static volatile uint8_t extended;
static uint8_t pressed[128];
static uint8_t pressed_ext[128];

static const char map_lower[58] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

static const char map_upper[58] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

static void push_key(int key)
{
    uint8_t next = (uint8_t)((head + 1) % BUF_SIZE);
    if (next == tail)
        return;
    buf[head] = key;
    head = next;
}

static void set_pressed(uint8_t *table, uint8_t code, uint8_t down)
{
    if (code < 128)
        table[code] = down;
}

static char apply_ctrl(char c)
{
    if (!ctrl)
        return c;
    if (c >= 'a' && c <= 'z')
        return (char)(c - 'a' + 1);
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 1);
    return c;
}

static void push_arrow(uint8_t code)
{
    if (code == 0x48)
        push_key(KBD_KEY_UP);
    else if (code == 0x50)
        push_key(KBD_KEY_DOWN);
    else if (code == 0x4B)
        push_key(KBD_KEY_LEFT);
    else if (code == 0x4D)
        push_key(KBD_KEY_RIGHT);
    else if (code == 0x47)
        push_key(KBD_KEY_HOME);
    else if (code == 0x4F)
        push_key(KBD_KEY_END);
    else if (code == 0x53)
        push_key(KBD_KEY_DELETE);
}

void kbd_irq_handler(void)
{
    uint8_t status = inb(0x64);
    uint8_t sc;
    uint8_t code;
    int make;

    if (!(status & 1))
        return;
    if (status & 0x20) {
        mouse_byte(inb(0x60));
        return;
    }

    sc = inb(0x60);
    code = sc & 0x7F;
    make = (sc & 0x80) == 0;

    if (sc == 0xE0) {
        extended = 1;
        return;
    }

    if (extended) {
        extended = 0;
        if (code == 0x1D) {
            ctrl = make ? 1 : 0;
            return;
        }
        if (code == 0x38) {
            alt = make ? 1 : 0;
            return;
        }
        if (make) {
            set_pressed(pressed_ext, code, 1);
            push_arrow(code);
        } else {
            set_pressed(pressed_ext, code, 0);
        }
        return;
    }

    if (code == 0x1D) {
        ctrl = make ? 1 : 0;
        return;
    }
    if (code == 0x38) {
        alt = make ? 1 : 0;
        return;
    }
    if (sc == 0x2A || sc == 0x36) {
        shift = 1;
        return;
    }
    if (sc == 0xAA || sc == 0xB6) {
        shift = 0;
        return;
    }

    if (make && sc < 58) {
        char c = shift ? map_upper[sc] : map_lower[sc];
        int was = pressed[code];
        if (c) {
            set_pressed(pressed, code, 1);
            if (alt && c == '\t') {
                if (!was)
                    push_key(KBD_ALT_TAB);
            } else if (alt && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
                if (c >= 'A' && c <= 'Z')
                    c = (char)(c - 'A' + 'a');
                if (!was)
                    push_key(KBD_META | (unsigned char)c);
            } else {
                c = apply_ctrl(c);
                push_key((int)(unsigned char)c);
            }
        }
    } else if (make && (code == 0x48 || code == 0x50 || code == 0x4B || code == 0x4D ||
                         code == 0x47 || code == 0x4F || code == 0x53)) {
        set_pressed(pressed_ext, code, 1);
        push_arrow(code);
    } else if (make && (code == 0x3B || code == 0x3C || code == 0x3D || code == 0x3E)) {
        if (!pressed[code]) {
            set_pressed(pressed, code, 1);
            if (alt && code == 0x3E)
                push_key(KBD_ALT_F4);
            else if (code == 0x3C)
                push_key(19);
            else if (code == 0x3D)
                push_key(24);
            else if (code == 0x3B)
                push_key(15);
        }
    } else if (!make) {
        set_pressed(pressed, code, 0);
        set_pressed(pressed_ext, code, 0);
    }
}

int kbd_pending(void)
{
    return head != tail;
}

void kbd_poll(void)
{
    int guard = 32;
    while (guard-- > 0) {
        uint8_t status = inb(0x64);
        if (!(status & 1))
            break;
        if (status & 0x20)
            break;
        kbd_irq_handler();
    }
}

int kbd_getchar(void)
{
    int key;

    for (;;) {
        __asm__ volatile ("sti");
        if (head != tail)
            break;
        kbd_poll();
        if (head == tail)
            __asm__ volatile ("hlt");
    }

    __asm__ volatile ("cli");
    key = buf[tail];
    tail = (uint8_t)((tail + 1) % BUF_SIZE);
    __asm__ volatile ("sti");

    return key;
}

int kbd_pop(void)
{
    int key;
    if (head == tail)
        return 0;
    key = buf[tail];
    tail = (uint8_t)((tail + 1) % BUF_SIZE);
    return key;
}
