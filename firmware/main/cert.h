#pragma once
extern const unsigned char server_cert_pem[];
extern const unsigned int server_cert_pem_len;
extern const unsigned char server_key_pem[];
extern const unsigned int server_key_pem_len;
int generate_runtime_cert(const unsigned char **cert, unsigned int *cert_len, const unsigned char **key, unsigned int *key_len);