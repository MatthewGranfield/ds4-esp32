#include "web_server.h"
#include "usb_hid_ds4.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "WEB";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

static ds4_action_t current_state = {
    .lx = 0x80, .ly = 0x80,
    .rx = 0x80, .ry = 0x80,
    .dpad = DS4_DPAD_NONE,
};

// Extrae el valor de una clave JSON string simple: {"key":"value",...}
static bool json_get_string(const char *json, const char *key, char *out, size_t out_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

// Comprueba si un campo booleano está presente y es true
static bool json_get_bool(const char *json, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    return strncmp(p, "true", 4) == 0;
}

// Extrae un entero. Devuelve true si lo encontró.
static bool json_get_int(const char *json, const char *key, int *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    char *endp = NULL;
    long v = strtol(p, &endp, 10);
    if (endp == p) return false;
    *out = (int)v;
    return true;
}

static void apply_button(const char *btn, bool press) {
    if (strcmp(btn, "ps") == 0) {
        if (press) current_state.buttons3 |= DS4_BTN_PS;
        else       current_state.buttons3 &= ~DS4_BTN_PS;
    } else if (strcmp(btn, "cross") == 0 || strcmp(btn, "x") == 0) {
        if (press) current_state.buttons1 |= DS4_BTN_CROSS;
        else       current_state.buttons1 &= ~DS4_BTN_CROSS;
    } else if (strcmp(btn, "circle") == 0) {
        if (press) current_state.buttons1 |= DS4_BTN_CIRCLE;
        else       current_state.buttons1 &= ~DS4_BTN_CIRCLE;
    } else if (strcmp(btn, "square") == 0) {
        if (press) current_state.buttons1 |= DS4_BTN_SQUARE;
        else       current_state.buttons1 &= ~DS4_BTN_SQUARE;
    } else if (strcmp(btn, "triangle") == 0) {
        if (press) current_state.buttons1 |= DS4_BTN_TRIANGLE;
        else       current_state.buttons1 &= ~DS4_BTN_TRIANGLE;
    } else if (strcmp(btn, "l1") == 0) {
        if (press) current_state.buttons2 |= DS4_BTN_L1;
        else       current_state.buttons2 &= ~DS4_BTN_L1;
    } else if (strcmp(btn, "r1") == 0) {
        if (press) current_state.buttons2 |= DS4_BTN_R1;
        else       current_state.buttons2 &= ~DS4_BTN_R1;
    } else if (strcmp(btn, "l2") == 0) {
        if (press) { current_state.buttons2 |= DS4_BTN_L2; current_state.lt = 0xFF; }
        else       { current_state.buttons2 &= ~DS4_BTN_L2; current_state.lt = 0; }
    } else if (strcmp(btn, "r2") == 0) {
        if (press) { current_state.buttons2 |= DS4_BTN_R2; current_state.rt = 0xFF; }
        else       { current_state.buttons2 &= ~DS4_BTN_R2; current_state.rt = 0; }
    } else if (strcmp(btn, "share") == 0) {
        if (press) current_state.buttons2 |= DS4_BTN_SHARE;
        else       current_state.buttons2 &= ~DS4_BTN_SHARE;
    } else if (strcmp(btn, "options") == 0) {
        if (press) current_state.buttons2 |= DS4_BTN_OPTIONS;
        else       current_state.buttons2 &= ~DS4_BTN_OPTIONS;
    } else if (strcmp(btn, "l3") == 0) {
        if (press) current_state.buttons2 |= DS4_BTN_L3;
        else       current_state.buttons2 &= ~DS4_BTN_L3;
    } else if (strcmp(btn, "r3") == 0) {
        if (press) current_state.buttons2 |= DS4_BTN_R3;
        else       current_state.buttons2 &= ~DS4_BTN_R3;
    } else if (strcmp(btn, "touchpad") == 0) {
        if (press) current_state.buttons3 |= DS4_BTN_TOUCHPAD;
        else       current_state.buttons3 &= ~DS4_BTN_TOUCHPAD;
    } else if (strcmp(btn, "up") == 0) {
        current_state.dpad = press ? DS4_DPAD_N : DS4_DPAD_NONE;
    } else if (strcmp(btn, "down") == 0) {
        current_state.dpad = press ? DS4_DPAD_S : DS4_DPAD_NONE;
    } else if (strcmp(btn, "left") == 0) {
        current_state.dpad = press ? DS4_DPAD_W : DS4_DPAD_NONE;
    } else if (strcmp(btn, "right") == 0) {
        current_state.dpad = press ? DS4_DPAD_E : DS4_DPAD_NONE;
    }
}

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start,
                    index_html_end - index_html_start);
    return ESP_OK;
}

static esp_err_t press_handler(httpd_req_t *req) {
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    char btn[32] = {0};
    if (!json_get_string(buf, "button", btn, sizeof(btn))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Campo button requerido");
        return ESP_FAIL;
    }

    bool hold = json_get_bool(buf, "hold");

    apply_button(btn, true);
    ds4_send_report(&current_state);

    if (!hold) {
        vTaskDelay(pdMS_TO_TICKS(100));
        apply_button(btn, false);
        ds4_send_report(&current_state);
    }

    ESP_LOGI(TAG, "Boton: %s hold=%d", btn, hold);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t release_handler(httpd_req_t *req) {
    memset(&current_state, 0, sizeof(current_state));
    current_state.lx = 0x80;
    current_state.ly = 0x80;
    current_state.rx = 0x80;
    current_state.ry = 0x80;
    current_state.dpad = DS4_DPAD_NONE;
    ds4_send_report(&current_state);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// Body: {"side":"l"|"r", "x":0..255, "y":0..255}
// Centro = 128. (0,0) = arriba-izq, (255,255) = abajo-der.
static esp_err_t stick_handler(httpd_req_t *req) {
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    char side[8] = {0};
    int x = 128, y = 128;
    json_get_string(buf, "side", side, sizeof(side));
    json_get_int(buf, "x", &x);
    json_get_int(buf, "y", &y);
    if (x < 0) x = 0;
    if (x > 255) x = 255;
    if (y < 0) y = 0;
    if (y > 255) y = 255;

    if (side[0] == 'r') {
        current_state.rx = (uint8_t)x;
        current_state.ry = (uint8_t)y;
    } else {
        current_state.lx = (uint8_t)x;
        current_state.ly = (uint8_t)y;
    }
    ds4_send_report(&current_state);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

void web_server_init(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port     = 7777;
    config.ctrl_port       = 32767;
    config.lru_purge_enable = true;
    config.max_open_sockets = 7;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start 7777 fallo (%s), reintentando en 2s...", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(2000));
        err = httpd_start(&server, &config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "No se pudo arrancar servidor 7777: %s", esp_err_to_name(err));
            return;
        }
    }

    httpd_uri_t uris[] = {
        {.uri="/",           .method=HTTP_GET,  .handler=index_handler},
        {.uri="/api/press",  .method=HTTP_POST, .handler=press_handler},
        {.uri="/api/release",.method=HTTP_POST, .handler=release_handler},
        {.uri="/api/stick",  .method=HTTP_POST, .handler=stick_handler},
    };
    for (int i = 0; i < (int)(sizeof(uris)/sizeof(uris[0])); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    ESP_LOGI(TAG, "Servidor web en puerto 7777");
}

