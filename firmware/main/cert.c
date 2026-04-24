#include <stddef.h>
#include "esp_err.h"

const unsigned char server_cert_pem[] = "";
const unsigned int server_cert_pem_len = 0;
const unsigned char server_key_pem[] = "";
const unsigned int server_key_pem_len = 0;

int generate_runtime_cert(const unsigned char **cert, unsigned int *cert_len, const unsigned char **key, unsigned int *key_len) {
    (void)cert;
    (void)cert_len;
    (void)key;
    (void)key_len;
    return ESP_ERR_NOT_SUPPORTED;
}