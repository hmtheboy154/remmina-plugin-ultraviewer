/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - Input Encoding
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "uv_input.h"

bool uv_encode_mouse_move(int x, int y, uint16_t flags,
                          uint8_t **out_payload, size_t *out_len)
{
    char str[128];
    snprintf(str, sizeof(str), "8|||512|||%u|||%d|||%d", (unsigned int)flags, x, y);
    size_t len = strlen(str);
    *out_payload = (uint8_t *)g_strdup(str);
    *out_len = len;
    return true;
}

bool uv_encode_mouse_button(uint32_t msg, int x, int y, uint16_t flags,
                            uint8_t **out_payload, size_t *out_len)
{
    int32_t packed = (int32_t)(((uint32_t)(x & 0xFFFF)) | (((uint32_t)(y & 0xFFFF)) << 16));
    char str[128];
    snprintf(str, sizeof(str), "8|||%u|||%u|||%d", msg, (unsigned int)flags, packed);
    size_t len = strlen(str);
    *out_payload = (uint8_t *)g_strdup(str);
    *out_len = len;
    return true;
}

bool uv_encode_mouse_wheel(int16_t delta, int x, int y, uint16_t flags,
                           uint8_t **out_payload, size_t *out_len)
{
    int32_t wparam = (int32_t)(((uint32_t)flags) | (((uint32_t)(delta & 0xFFFF)) << 16));
    int32_t packed = (int32_t)(((uint32_t)(x & 0xFFFF)) | (((uint32_t)(y & 0xFFFF)) << 16));
    char str[128];
    snprintf(str, sizeof(str), "8|||522|||%d|||%d", wparam, packed);
    size_t len = strlen(str);
    *out_payload = (uint8_t *)g_strdup(str);
    *out_len = len;
    return true;
}

typedef struct {
    guint keyval;
    uint8_t vk;
    uint16_t scan;
    uint16_t ch;
} KeyTableEntry;

static const KeyTableEntry KEY_TABLE[] = {
    /* Alphabet */
    { GDK_KEY_a, 0x41, 0x1E, 'a' }, { GDK_KEY_A, 0x41, 0x1E, 'A' },
    { GDK_KEY_b, 0x42, 0x30, 'b' }, { GDK_KEY_B, 0x42, 0x30, 'B' },
    { GDK_KEY_c, 0x43, 0x2E, 'c' }, { GDK_KEY_C, 0x43, 0x2E, 'C' },
    { GDK_KEY_d, 0x44, 0x20, 'd' }, { GDK_KEY_D, 0x44, 0x20, 'D' },
    { GDK_KEY_e, 0x45, 0x12, 'e' }, { GDK_KEY_E, 0x45, 0x12, 'E' },
    { GDK_KEY_f, 0x46, 0x21, 'f' }, { GDK_KEY_F, 0x46, 0x21, 'F' },
    { GDK_KEY_g, 0x47, 0x22, 'g' }, { GDK_KEY_G, 0x47, 0x22, 'G' },
    { GDK_KEY_h, 0x48, 0x23, 'h' }, { GDK_KEY_H, 0x48, 0x23, 'H' },
    { GDK_KEY_i, 0x49, 0x17, 'i' }, { GDK_KEY_I, 0x49, 0x17, 'I' },
    { GDK_KEY_j, 0x4A, 0x24, 'j' }, { GDK_KEY_J, 0x4A, 0x24, 'J' },
    { GDK_KEY_k, 0x4B, 0x25, 'k' }, { GDK_KEY_K, 0x4B, 0x25, 'K' },
    { GDK_KEY_l, 0x4C, 0x26, 'l' }, { GDK_KEY_L, 0x4C, 0x26, 'L' },
    { GDK_KEY_m, 0x4D, 0x32, 'm' }, { GDK_KEY_M, 0x4D, 0x32, 'M' },
    { GDK_KEY_n, 0x4E, 0x31, 'n' }, { GDK_KEY_N, 0x4E, 0x31, 'N' },
    { GDK_KEY_o, 0x4F, 0x18, 'o' }, { GDK_KEY_O, 0x4F, 0x18, 'O' },
    { GDK_KEY_p, 0x50, 0x19, 'p' }, { GDK_KEY_P, 0x50, 0x19, 'P' },
    { GDK_KEY_q, 0x51, 0x10, 'q' }, { GDK_KEY_Q, 0x51, 0x10, 'Q' },
    { GDK_KEY_r, 0x52, 0x13, 'r' }, { GDK_KEY_R, 0x52, 0x13, 'R' },
    { GDK_KEY_s, 0x53, 0x1F, 's' }, { GDK_KEY_S, 0x53, 0x1F, 'S' },
    { GDK_KEY_t, 0x54, 0x14, 't' }, { GDK_KEY_T, 0x54, 0x14, 'T' },
    { GDK_KEY_u, 0x55, 0x16, 'u' }, { GDK_KEY_U, 0x55, 0x16, 'U' },
    { GDK_KEY_v, 0x56, 0x2F, 'v' }, { GDK_KEY_V, 0x56, 0x2F, 'V' },
    { GDK_KEY_w, 0x57, 0x11, 'w' }, { GDK_KEY_W, 0x57, 0x11, 'W' },
    { GDK_KEY_x, 0x58, 0x2D, 'x' }, { GDK_KEY_X, 0x58, 0x2D, 'X' },
    { GDK_KEY_y, 0x59, 0x15, 'y' }, { GDK_KEY_Y, 0x59, 0x15, 'Y' },
    { GDK_KEY_z, 0x5A, 0x2C, 'z' }, { GDK_KEY_Z, 0x5A, 0x2C, 'Z' },

    /* Digits */
    { GDK_KEY_0, 0x30, 0x0B, '0' }, { GDK_KEY_parenright, 0x30, 0x0B, ')' },
    { GDK_KEY_1, 0x31, 0x02, '1' }, { GDK_KEY_exclam,     0x31, 0x02, '!' },
    { GDK_KEY_2, 0x32, 0x03, '2' }, { GDK_KEY_at,         0x32, 0x03, '@' },
    { GDK_KEY_3, 0x33, 0x04, '3' }, { GDK_KEY_numbersign, 0x33, 0x04, '#' },
    { GDK_KEY_4, 0x34, 0x05, '4' }, { GDK_KEY_dollar,     0x34, 0x05, '$' },
    { GDK_KEY_5, 0x35, 0x06, '5' }, { GDK_KEY_percent,    0x35, 0x06, '%' },
    { GDK_KEY_6, 0x36, 0x07, '6' }, { GDK_KEY_asciicircum,0x36, 0x07, '^' },
    { GDK_KEY_7, 0x37, 0x08, '7' }, { GDK_KEY_ampersand,  0x37, 0x08, '&' },
    { GDK_KEY_8, 0x38, 0x09, '8' }, { GDK_KEY_asterisk,   0x38, 0x09, '*' },
    { GDK_KEY_9, 0x39, 0x0A, '9' }, { GDK_KEY_parenleft,  0x39, 0x0A, '(' },

    /* Standard Punctuation */
    { GDK_KEY_space,        0x20, 0x39, ' ' },
    { GDK_KEY_minus,        0xBD, 0x0C, '-' }, { GDK_KEY_underscore, 0xBD, 0x0C, '_' },
    { GDK_KEY_equal,        0xBB, 0x0D, '=' }, { GDK_KEY_plus,       0xBB, 0x0D, '+' },
    { GDK_KEY_bracketleft,  0xDB, 0x1A, '[' }, { GDK_KEY_braceleft,  0xDB, 0x1A, '{' },
    { GDK_KEY_bracketright, 0xDD, 0x1B, ']' }, { GDK_KEY_braceright, 0xDD, 0x1B, '}' },
    { GDK_KEY_backslash,    0xDC, 0x2B, '\\'}, { GDK_KEY_bar,        0xDC, 0x2B, '|' },
    { GDK_KEY_semicolon,    0xBA, 0x27, ';' }, { GDK_KEY_colon,      0xBA, 0x27, ':' },
    { GDK_KEY_apostrophe,   0xDE, 0x28, '\'' }, { GDK_KEY_quotedbl,   0xDE, 0x28, '"' },
    { GDK_KEY_grave,        0xC0, 0x29, '`' }, { GDK_KEY_asciitilde, 0xC0, 0x29, '~' },
    { GDK_KEY_comma,        0xBC, 0x33, ',' }, { GDK_KEY_less,       0xBC, 0x33, '<' },
    { GDK_KEY_period,       0xBE, 0x34, '.' }, { GDK_KEY_greater,    0xBE, 0x34, '>' },
    { GDK_KEY_slash,        0xBF, 0x35, '/' }, { GDK_KEY_question,   0xBF, 0x35, '?' },

    /* Controls */
    { GDK_KEY_Return,       0x0D, 0x1C, 13 },
    { GDK_KEY_Tab,          0x09, 0x0F, 9 },
    { GDK_KEY_BackSpace,    0x08, 0x0E, 8 },
    { GDK_KEY_Escape,       0x1B, 0x01, 27 },

    /* Modifiers (Windows specific VK_L* / VK_R*) */
    { GDK_KEY_Shift_L,      0xA0, 0x2A, 0 },
    { GDK_KEY_Shift_R,      0xA1, 0x36, 0 },
    { GDK_KEY_Control_L,    0xA2, 0x1D, 0 },
    { GDK_KEY_Control_R,    0xA3, 0xE01D, 0 },
    { GDK_KEY_Alt_L,        0xA4, 0x38, 0 },
    { GDK_KEY_Alt_R,        0xA5, 0xE038, 0 },
    { GDK_KEY_Super_L,      0x5B, 0xE05B, 0 },
    { GDK_KEY_Super_R,      0x5C, 0xE05C, 0 },
    { GDK_KEY_Caps_Lock,    0x14, 0x3A, 0 },

    /* Navigation & Editing (Extended keys) */
    { GDK_KEY_Insert,       0x2D, 0xE052, 0 },
    { GDK_KEY_Delete,       0x2E, 0xE053, 0 },
    { GDK_KEY_Home,         0x24, 0xE047, 0 },
    { GDK_KEY_End,          0x23, 0xE04F, 0 },
    { GDK_KEY_Page_Up,      0x21, 0xE049, 0 },
    { GDK_KEY_Page_Down,    0x22, 0xE051, 0 },
    { GDK_KEY_Left,         0x25, 0xE04B, 0 },
    { GDK_KEY_Up,           0x26, 0xE048, 0 },
    { GDK_KEY_Right,        0x27, 0xE04D, 0 },
    { GDK_KEY_Down,         0x28, 0xE050, 0 },

    /* Function Keys */
    { GDK_KEY_F1,  0x70, 0x3B, 0 }, { GDK_KEY_F2,  0x71, 0x3C, 0 },
    { GDK_KEY_F3,  0x72, 0x3D, 0 }, { GDK_KEY_F4,  0x73, 0x3E, 0 },
    { GDK_KEY_F5,  0x74, 0x3F, 0 }, { GDK_KEY_F6,  0x75, 0x40, 0 },
    { GDK_KEY_F7,  0x76, 0x41, 0 }, { GDK_KEY_F8,  0x77, 0x42, 0 },
    { GDK_KEY_F9,  0x78, 0x43, 0 }, { GDK_KEY_F10, 0x79, 0x44, 0 },
    { GDK_KEY_F11, 0x7A, 0x57, 0 }, { GDK_KEY_F12, 0x7B, 0x58, 0 },
};

bool uv_encode_key(guint keyval, GdkModifierType state, bool is_press,
                   uint8_t **out_payload, size_t *out_len)
{
    uint8_t vk = 0;
    uint16_t scan = 0;
    uint16_t ch = 0;
    bool found = false;

    for (size_t i = 0; i < G_N_ELEMENTS(KEY_TABLE); i++) {
        if (KEY_TABLE[i].keyval == keyval) {
            vk = KEY_TABLE[i].vk;
            scan = KEY_TABLE[i].scan;
            ch = KEY_TABLE[i].ch;
            found = true;
            break;
        }
    }

    /* Fallback: Unicode character via VK_PACKET */
    if (!found) {
        guint32 unicode = gdk_keyval_to_unicode(keyval);
        if (unicode >= 0x20 && unicode <= 0xFFFF) {
            return uv_encode_unicode_char((uint16_t)unicode, is_press, (state & GDK_LOCK_MASK) != 0,
                                          out_payload, out_len);
        }
        return false;
    }

    /* When Control, Alt, or Super is active, suppress character code to trigger host hotkeys */
    if ((state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK)) != 0 || !is_press) {
        ch = 0;
    }

    bool alt_held = (state & GDK_MOD1_MASK) != 0 || (keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R);
    uint8_t kind;
    if (is_press) {
        kind = alt_held ? UV_SYS_KEY_DOWN : UV_KEY_DOWN;
    } else {
        kind = alt_held ? UV_SYS_KEY_UP : UV_KEY_UP;
    }

    uint8_t caps_lock = (state & GDK_LOCK_MASK) ? 1 : 0;

    uint8_t *pkt = g_malloc(11);
    pkt[0] = 30;
    pkt[1] = 0x7C;
    pkt[2] = 0x7C;
    pkt[3] = 0x7C;
    pkt[4] = kind;
    pkt[5] = vk;
    uv_write_u16_le(pkt + 6, scan);
    uv_write_u16_le(pkt + 8, ch);
    pkt[10] = caps_lock;

    *out_payload = pkt;
    *out_len = 11;
    return true;
}

bool uv_encode_unicode_char(uint16_t ch, bool is_press, bool caps_lock,
                            uint8_t **out_payload, size_t *out_len)
{
    uint8_t *pkt = g_malloc(11);
    pkt[0] = 30;
    pkt[1] = 0x7C;
    pkt[2] = 0x7C;
    pkt[3] = 0x7C;
    pkt[4] = is_press ? UV_KEY_DOWN : UV_KEY_UP;
    pkt[5] = UV_VK_PACKET;
    uv_write_u16_le(pkt + 6, 0);
    uv_write_u16_le(pkt + 8, ch);
    pkt[10] = caps_lock ? 1 : 0;

    *out_payload = pkt;
    *out_len = 11;
    return true;
}
