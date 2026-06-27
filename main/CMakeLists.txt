idf_component_register(
    SRCS "main.c" "usb_hid_ds4.c" "web_server.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi esp_http_server nvs_flash esp_event
    EMBED_FILES "web/index.html"
)
