idf_component_register(
    SRCS "app_main.c" "cert.c"
    INCLUDE_DIRS "."
    REQUIRES esp_https_server esp_http_server esp_wifi nvs_flash esp_netif spi_flash spiffs driver mbedtls
    EMBED_TXTFILES "certs/servercert.pem" "certs/prvtkey.pem"
)