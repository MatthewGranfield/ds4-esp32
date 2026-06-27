#include "usb_hid_ds4.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DS4_HID";
static uint8_t report_counter = 0;

// Estado de auth
static bool    auth_nonce_received = false;
static uint8_t auth_nonce_id       = 0;

// ════════════════════════════════════════════════════════════════════════════
// HID REPORT DESCRIPTOR — clonado de GP2040-CE (PS4 mode)
// 489 bytes con ~40 reports declarados. La PS5 espera ver este espacio
// completo o rechaza el dispositivo silenciosamente.
// ════════════════════════════════════════════════════════════════════════════
const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1) — INPUT report (botones+sticks)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    0x09, 0x39,        //   Usage (Hat switch)
    0x15, 0x00,
    0x25, 0x07,
    0x35, 0x00,
    0x46, 0x3B, 0x01,
    0x65, 0x14,
    0x75, 0x04,
    0x95, 0x01,
    0x81, 0x42,

    0x65, 0x00,        //   Unit (None)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,
    0x29, 0x0E,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x0E,
    0x81, 0x02,

    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x20,
    0x75, 0x06,
    0x95, 0x01,
    0x81, 0x02,

    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x33,
    0x09, 0x34,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x02,
    0x81, 0x02,

    0x06, 0x00, 0xFF,
    0x09, 0x21,
    0x95, 0x36,
    0x81, 0x02,

    0x85, 0x05,        //   Report ID (5) OUTPUT — rumble/LED
    0x09, 0x22,
    0x95, 0x1F,
    0x91, 0x02,

    0x85, 0x03,        //   Report ID (3) FEATURE — controller definition
    0x0A, 0x21, 0x27,
    0x95, 0x2F,
    0xB1, 0x02,

    0x85, 0x02,        //   Report ID (2) FEATURE — calibration
    0x09, 0x24,
    0x95, 0x24,
    0xB1, 0x02,

    0x85, 0x08, 0x09, 0x25, 0x95, 0x03, 0xB1, 0x02,
    0x85, 0x10, 0x09, 0x26, 0x95, 0x04, 0xB1, 0x02,
    0x85, 0x11, 0x09, 0x27, 0x95, 0x02, 0xB1, 0x02,

    0x85, 0x12,        //   Report ID (18) FEATURE — MAC address
    0x06, 0x02, 0xFF,
    0x09, 0x21,
    0x95, 0x0F,
    0xB1, 0x02,

    0x85, 0x13, 0x09, 0x22, 0x95, 0x16, 0xB1, 0x02,
    0x85, 0x14, 0x06, 0x05, 0xFF, 0x09, 0x20, 0x95, 0x10, 0xB1, 0x02,
    0x85, 0x15, 0x09, 0x21, 0x95, 0x2C, 0xB1, 0x02,

    0x06, 0x80, 0xFF,
    0x85, 0x80, 0x09, 0x20, 0x95, 0x06, 0xB1, 0x02,
    0x85, 0x81, 0x09, 0x21, 0x95, 0x06, 0xB1, 0x02,
    0x85, 0x82, 0x09, 0x22, 0x95, 0x05, 0xB1, 0x02,
    0x85, 0x83, 0x09, 0x23, 0x95, 0x01, 0xB1, 0x02,
    0x85, 0x84, 0x09, 0x24, 0x95, 0x04, 0xB1, 0x02,
    0x85, 0x85, 0x09, 0x25, 0x95, 0x06, 0xB1, 0x02,
    0x85, 0x86, 0x09, 0x26, 0x95, 0x06, 0xB1, 0x02,
    0x85, 0x87, 0x09, 0x27, 0x95, 0x23, 0xB1, 0x02,
    0x85, 0x88, 0x09, 0x28, 0x95, 0x22, 0xB1, 0x02,
    0x85, 0x89, 0x09, 0x29, 0x95, 0x02, 0xB1, 0x02,
    0x85, 0x90, 0x09, 0x30, 0x95, 0x05, 0xB1, 0x02,
    0x85, 0x91, 0x09, 0x31, 0x95, 0x03, 0xB1, 0x02,
    0x85, 0x92, 0x09, 0x32, 0x95, 0x03, 0xB1, 0x02,
    0x85, 0x93, 0x09, 0x33, 0x95, 0x0C, 0xB1, 0x02,
    0x85, 0xA0, 0x09, 0x40, 0x95, 0x06, 0xB1, 0x02,
    0x85, 0xA1, 0x09, 0x41, 0x95, 0x01, 0xB1, 0x02,
    0x85, 0xA2, 0x09, 0x42, 0x95, 0x01, 0xB1, 0x02,

    0x85, 0xA3,        //   Report ID (163) FEATURE — version/date
    0x09, 0x43,
    0x95, 0x30,
    0xB1, 0x02,

    0x85, 0xA4, 0x09, 0x44, 0x95, 0x0D, 0xB1, 0x02,
    0x85, 0xA5, 0x09, 0x45, 0x95, 0x15, 0xB1, 0x02,
    0x85, 0xA6, 0x09, 0x46, 0x95, 0x15, 0xB1, 0x02,
    0x85, 0xA7, 0x09, 0x4A, 0x95, 0x01, 0xB1, 0x02,
    0x85, 0xA8, 0x09, 0x4B, 0x95, 0x01, 0xB1, 0x02,
    0x85, 0xA9, 0x09, 0x4C, 0x95, 0x08, 0xB1, 0x02,
    0x85, 0xAA, 0x09, 0x4E, 0x95, 0x01, 0xB1, 0x02,
    0x85, 0xAB, 0x09, 0x4F, 0x95, 0x39, 0xB1, 0x02,
    0x85, 0xAC, 0x09, 0x50, 0x95, 0x39, 0xB1, 0x02,
    0x85, 0xAD, 0x09, 0x51, 0x95, 0x0B, 0xB1, 0x02,
    0x85, 0xAE, 0x09, 0x52, 0x95, 0x01, 0xB1, 0x02,
    0x85, 0xAF, 0x09, 0x53, 0x95, 0x02, 0xB1, 0x02,
    0x85, 0xB0, 0x09, 0x54, 0x95, 0x3F, 0xB1, 0x02,
    0xC0,              // End Collection

    // Auth section (vendor-defined)
    0x06, 0xF0, 0xFF,
    0x09, 0x40,
    0xA1, 0x01,
    0x85, 0xF0,        //   Report ID (0xF0) — auth challenge in
    0x09, 0x47,
    0x95, 0x3F,
    0xB1, 0x02,
    0x85, 0xF1,        //   Report ID (0xF1) — signature out
    0x09, 0x48,
    0x95, 0x3F,
    0xB1, 0x02,
    0x85, 0xF2,        //   Report ID (0xF2) — auth status
    0x09, 0x49,
    0x95, 0x0F,
    0xB1, 0x02,
    0x85, 0xF3,        //   Report ID (0xF3) — reset auth
    0x0A, 0x01, 0x47,
    0x95, 0x07,
    0xB1, 0x02,
    0xC0,
};

// ════════════════════════════════════════════════════════════════════════════
// STRING DESCRIPTORS — usamos los de GP2040-CE (Razer Panthera licenciado)
// VID/PID 0x1532/0x0401 (Razer Panthera) → la PS5 lo trata como mando
// licenciado oficialmente y no exige firma RSA estricta como con DS4 real.
// ════════════════════════════════════════════════════════════════════════════
const char *hid_string_descriptor[] = {
    (char[]){0x09, 0x04},                       // 0: lang English
    "HORI CO.,LTD.",                            // 1: manufacturer
    "HORI Fighting Commander OCTA",             // 2: product
    "0000000000000001",                         // 3: serial
};

// ÁHID descriptor con DOS endpoints (IN 0x81 + OUT 0x03), igual que un mando
// licenciado PS4 real. El endpoint OUT es necesario para que la PS5 envíe
// el output report (rumble/LED) tras el handshake.
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    // iface=0, str_idx=2, boot_protocol=0(none), report_desc_len, ep_out=0x03, ep_in=0x81, ep_size=64, poll=1
    TUD_HID_INOUT_DESCRIPTOR(0, 2, HID_ITF_PROTOCOL_NONE,
                              sizeof(hid_report_descriptor),
                              0x03, 0x81, 64, 1),
};

static const tusb_desc_device_t ds4_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = 0x0F0D,   // HORI
    .idProduct          = 0x0163,   // Fighting Commander OCTA (modo PS5)
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x00,     // sin serial → como Razer Panthera real
    .bNumConfigurations = 0x01,
};

// ════════════════════════════════════════════════════════════════════════════
// RESPUESTAS A FEATURE REPORTS (datos extraídos de GP2040-CE)
// La PS5 pide estos reports en orden durante el "handshake" inicial. Sin
// responder correctamente NO llega a la fase de auth ni acepta el mando.
// ════════════════════════════════════════════════════════════════════════════

// 0x02 GET_CALIBRATION — 36 bytes
static const uint8_t out_0x02[] = {
    0xfe, 0xff, 0x0e, 0x00, 0x04, 0x00, 0xd4, 0x22,
    0x2a, 0xdd, 0xbb, 0x22, 0x5e, 0xdd, 0x81, 0x22,
    0x84, 0xdd, 0x1c, 0x02, 0x1c, 0x02, 0x85, 0x1f,
    0xb0, 0xe0, 0xc6, 0x20, 0xb5, 0xe0, 0xb1, 0x20,
    0x83, 0xdf, 0x0c, 0x00,
};

// 0x03 DEFINITION — 47 bytes — layout exacto de PS4ControllerConfig:
//   hidUsage(2) + mystery0(1) + features(1) + controllerType(1) +
//   touchpadParam(2) + imuConfig(10) + magicID(2) +
//   mystery1(4) + wheelParam(3) + mystery2(21) = 47
// Layout: hidUsage(2) + mystery0(1) + features(1) + controllerType(1) +
//         touchpadParam(2) + imuConfig(10) + magicID(2) +
//         mystery1(4) + wheelParam(3) + mystery2(21) = 47
// Nota: el sample "wire format" de GP2040-CE empieza con 0x21, 0x27 — eso es
// porque struct PS4ControllerConfig tiene hidUsage = 0x2721 inicializado
// (no 0x2127). Lo dejamos como 0x21, 0x27 = 0x2721 LE.
static const uint8_t out_0x03[] = {
    0x21, 0x27,                                      // hidUsage
    0x04,                                            // mystery0
    0xEF,                                            // features
    0x01,                                            // controllerType = DS4
    0x2C, 0x56,                                      // touchpadParam
    0x08, 0x00, 0x3D, 0x00, 0xE8, 0x03, 0x04, 0x00, 0xFF, 0x7F, // imuConfig
    0x0D, 0x0D,                                      // magicID
    0x00, 0x00, 0x00, 0x00,                          // mystery1
    0x0D, 0x84, 0x03,                                // wheelParam
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        // mystery2 (21 bytes)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
_Static_assert(sizeof(out_0x03) == 47, "out_0x03 debe medir 47 bytes");

// 0x12 GET_MAC_ADDRESS — 16 bytes
static const uint8_t out_0x12[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x25,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// 0xA3 GET_VERSION_DATE — 48 bytes
static const uint8_t out_0xA3[] = {
    0x4A, 0x75, 0x6E, 0x20, 0x20, 0x39, 0x20, 0x32,
    0x30, 0x31, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x31, 0x32, 0x3A, 0x33, 0x36, 0x3A, 0x34, 0x31,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x08, 0xB4, 0x01, 0x00, 0x00, 0x00,
    0x07, 0xA0, 0x10, 0x20, 0x00, 0xA0, 0x02, 0x00,
};

// ════════════════════════════════════════════════════════════════════════════
// CALLBACKS TINYUSB
// ════════════════════════════════════════════════════════════════════════════

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    ESP_LOGI(TAG, "HOST: pidió HID report descriptor (instance %d)", instance);
    return hid_report_descriptor;
}

// Callback que TinyUSB llama cuando completa el envío de un report.
// La PS5 espera que enviemos reports a 1ms; este callback se usa para
// encolar el siguiente.
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len) {
    (void)instance;
    (void)report;
    (void)len;
    // No hacemos nada aquí — el hid_task envía a ritmo fijo.
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen) {
    ESP_LOGI(TAG, "HOST: GET_REPORT id=0x%02X type=%d len=%u",
             report_id, report_type, reqlen);
    if (report_type != HID_REPORT_TYPE_FEATURE) return 0;

    switch (report_id) {
        case 0x02: {  // GET_CALIBRATION
            uint16_t n = reqlen < sizeof(out_0x02) ? reqlen : sizeof(out_0x02);
            memcpy(buffer, out_0x02, n);
            return n;
        }
        case 0x03: {  // DEFINITION — devolver SIEMPRE 47 bytes
            if (reqlen < sizeof(out_0x03)) {
                ESP_LOGW(TAG, "0x03: reqlen=%u < 47, devolviendo 0", reqlen);
                return 0;
            }
            memcpy(buffer, out_0x03, sizeof(out_0x03));
            ESP_LOGI(TAG, "STUB: 0x03 DEFINITION respondida (47 bytes)");
            return sizeof(out_0x03);
        }
        case 0x12: {  // GET_MAC_ADDRESS
            uint16_t n = reqlen < sizeof(out_0x12) ? reqlen : sizeof(out_0x12);
            memcpy(buffer, out_0x12, n);
            return n;
        }
        case 0xA3: {  // GET_VERSION_DATE
            uint16_t n = reqlen < sizeof(out_0xA3) ? reqlen : sizeof(out_0xA3);
            memcpy(buffer, out_0xA3, n);
            return n;
        }
        case 0xF1: {  // AUTH SIGNATURE RESPONSE (stub: ceros)
            memset(buffer, 0, reqlen);
            buffer[0] = auth_nonce_id;
            ESP_LOGW(TAG, "STUB: F1 auth signature (nonce_id=0x%02x)", auth_nonce_id);
            return reqlen;
        }
        case 0xF2: {  // AUTH STATUS (stub: 0x00 = OK)
            memset(buffer, 0, reqlen);
            buffer[0] = auth_nonce_id;
            buffer[1] = 0x00;
            ESP_LOGW(TAG, "STUB: F2 auth status OK");
            return reqlen;
        }
        default:
            ESP_LOGW(TAG, "GET_REPORT id=0x%02X no manejado, devolviendo ceros", report_id);
            memset(buffer, 0, reqlen);
            return reqlen;
    }
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const *buffer, uint16_t bufsize) {
    ESP_LOGI(TAG, "HOST: SET_REPORT id=0x%02X type=%d len=%u",
             report_id, report_type, bufsize);
    if (report_type != HID_REPORT_TYPE_FEATURE) return;

    if (report_id == 0xF0 && bufsize >= 2) {
        auth_nonce_id       = buffer[0];
        auth_nonce_received = true;
        ESP_LOGI(TAG, "SET F0 (auth nonce, id=0x%02x)", auth_nonce_id);
    }
}

void usb_hid_ds4_init(void) {
    ESP_LOGI(TAG, "Iniciando USB HID (Razer Panthera PID 0x0401)");

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device            = &ds4_device_descriptor;
    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
    tusb_cfg.descriptor.string            = hid_string_descriptor;
    tusb_cfg.descriptor.string_count      = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB HID listo — la PS5 ahora debería pedir 0x02/0x03/0x12/0xA3");
}

void ds4_send_report(const ds4_action_t *action) {
    if (!tud_hid_ready()) return;

    ds4_report_t report = {0};
    report.report_id = 0x01;
    report.lx = action->lx;
    report.ly = action->ly;
    report.rx = action->rx;
    report.ry = action->ry;
    report.buttons1 = (action->buttons1 & 0xF0) | (action->dpad & 0x0F);
    report.buttons2 = action->buttons2;
    report.buttons3 = ((report_counter++ & 0x3F) << 2) | (action->buttons3 & 0x03);
    report.lt = action->lt;
    report.rt = action->rt;

    // El report ID ya está dentro del buffer (byte 0). Pasamos 0 para que
    // TinyUSB no lo prependa otra vez (igual que hace GP2040-CE).
    tud_hid_report(0, &report, sizeof(report));
}

void ds4_send_empty(void) {
    ds4_action_t empty = {
        .lx = 0x80, .ly = 0x80,
        .rx = 0x80, .ry = 0x80,
        .dpad = DS4_DPAD_NONE,
    };
    ds4_send_report(&empty);
}
