#pragma once
#include <stdint.h>
#include <stdbool.h>

// DS4 button bitmasks para el byte 5 (upper nibble = face buttons, lower nibble = dpad)
#define DS4_BTN_SQUARE    0x10
#define DS4_BTN_CROSS     0x20
#define DS4_BTN_CIRCLE    0x40
#define DS4_BTN_TRIANGLE  0x80

// DS4 byte 6 buttons
#define DS4_BTN_L1        0x01
#define DS4_BTN_R1        0x02
#define DS4_BTN_L2        0x04
#define DS4_BTN_R2        0x08
#define DS4_BTN_SHARE     0x10
#define DS4_BTN_OPTIONS   0x20
#define DS4_BTN_L3        0x40
#define DS4_BTN_R3        0x80

// DS4 byte 7
#define DS4_BTN_PS        0x01
#define DS4_BTN_TOUCHPAD  0x02

// Dpad values (lower nibble de byte 5)
#define DS4_DPAD_N        0x00
#define DS4_DPAD_NE       0x01
#define DS4_DPAD_E        0x02
#define DS4_DPAD_SE       0x03
#define DS4_DPAD_S        0x04
#define DS4_DPAD_SW       0x05
#define DS4_DPAD_W        0x06
#define DS4_DPAD_NW       0x07
#define DS4_DPAD_NONE     0x08

typedef struct {
    uint8_t report_id;   // siempre 0x01
    uint8_t lx;          // left stick X, 0x80 = centro
    uint8_t ly;          // left stick Y, 0x80 = centro
    uint8_t rx;          // right stick X
    uint8_t ry;          // right stick Y
    uint8_t buttons1;    // dpad (nibble bajo) + square/cross/circle/triangle (nibble alto)
    uint8_t buttons2;    // L1 R1 L2 R2 share options L3 R3
    uint8_t buttons3;    // counter(7:2) touchpad(1) ps(0)
    uint8_t lt;          // left trigger
    uint8_t rt;          // right trigger
    uint8_t reserved[54]; // gyro/accel/touchpad/relleno hasta 64 bytes
} __attribute__((packed)) ds4_report_t;
_Static_assert(sizeof(ds4_report_t) == 64, "Wired DS4 input report must be 64 bytes");

// Accion que puede pedir la web
typedef struct {
    uint8_t buttons1;
    uint8_t buttons2;
    uint8_t buttons3;
    uint8_t dpad;
    uint8_t lx, ly, rx, ry;
    uint8_t lt, rt;
} ds4_action_t;

void usb_hid_ds4_init(void);
bool ds4_send_report(const ds4_action_t *action);
