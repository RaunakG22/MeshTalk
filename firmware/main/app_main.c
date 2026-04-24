#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include <stdlib.h>
#include "sdkconfig.h"
#include "cert.h"
#include <string.h>

static const char *TAG = "app";
static void ws_free_cb(esp_err_t err, int sock, void *arg) { (void)err; (void)sock; free(arg); }

typedef struct {
    bool wifi_ap_ok;
    bool https_ok;
    bool spiffs_ok;
    bool ws_ok;
    bool camera_ok;
    bool audio_ok;
} diag_t;

static diag_t g_diag = {0};

typedef struct {
    int sockfd;
    char id[64];
    char name[64];
} client_t;

#define MAX_CLIENTS 24
static client_t clients[MAX_CLIENTS];

static httpd_handle_t server = NULL; // HTTPS server
static httpd_handle_t server_http = NULL; // HTTP redirect server
static bool audio_task_running = false;
static int audio_clients[MAX_CLIENTS];

static void broadcast_peers();
static void remove_client(int sockfd);

static esp_err_t on_open(httpd_handle_t hd, int sockfd) { return ESP_OK; }
static void on_close(httpd_handle_t hd, int sockfd) {
    remove_client(sockfd);
    for (int i = 0; i < MAX_CLIENTS; i++) if (audio_clients[i] == sockfd) audio_clients[i] = 0;
    broadcast_peers();
}

static void add_client(int sockfd, const char *id, const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd && id && strcmp(clients[i].id, id) == 0) {
            clients[i].sockfd = sockfd;
            strncpy(clients[i].id, id, sizeof(clients[i].id) - 1);
            strncpy(clients[i].name, name ? name : "", sizeof(clients[i].name) - 1);
            return;
        }
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd == 0) {
            clients[i].sockfd = sockfd;
            strncpy(clients[i].id, id ? id : "", sizeof(clients[i].id) - 1);
            strncpy(clients[i].name, name ? name : "", sizeof(clients[i].name) - 1);
            return;
        }
    }
}

static void remove_client(int sockfd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd == sockfd) {
            memset(&clients[i], 0, sizeof(client_t));
            return;
        }
    }
}

static void broadcast_peers() {
    char *buf = (char *)malloc(2048);
    size_t pos = 0;
    pos += snprintf(buf + pos, 2048 - pos, "{\"type\":\"peers\",\"peers\":[");
    bool first = true;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd) {
            if (!first) pos += snprintf(buf + pos, 2048 - pos, ",");
            pos += snprintf(buf + pos, 2048 - pos, "{\"id\":\"%s\",\"name\":\"%s\"}", clients[i].id, clients[i].name);
            first = false;
        }
    }
    pos += snprintf(buf + pos, 2048 - pos, "]}");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd) {
            uint8_t *copy = (uint8_t *)malloc(pos);
            memcpy(copy, buf, pos);
            httpd_ws_frame_t frame = (httpd_ws_frame_t){0};
            frame.type = HTTPD_WS_TYPE_TEXT;
            frame.len = pos;
            frame.payload = copy;
            httpd_ws_send_data_async(server, clients[i].sockfd, &frame, ws_free_cb, copy);
        }
    }
    free(buf);
}

static char *json_get_string(const char *json, const char *key) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p = strchr(p, ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return NULL;
    p++;
    const char *q = strchr(p, '"');
    if (!q) return NULL;
    size_t len = (size_t)(q - p);
    char *out = (char *)malloc(len + 1);
    memcpy(out, p, len);
    out[len] = 0;
    return out;
}

static bool json_get_bool(const char *json, const char *key, bool *out) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "true", 4) == 0) { *out = true; return true; }
    if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static client_t *find_client_by_id(const char *id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd && strcmp(clients[i].id, id) == 0) return &clients[i];
    }
    return NULL;
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) return ESP_OK;
    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;
    httpd_ws_recv_frame(req, &frame, 0);
    if (!frame.len) return ESP_OK;
    uint8_t *buf = (uint8_t *)malloc(frame.len + 1);
    frame.payload = buf;
    httpd_ws_recv_frame(req, &frame, frame.len);
    buf[frame.len] = 0;
    int sockfd = httpd_req_to_sockfd(req);
    char *json_text = (char *)buf;
    char *type = json_get_string((char *)json_text, "type");
    if (type) {
        if (strcmp(type, "join") == 0) {
            char *id = json_get_string((char *)json_text, "id");
            char *name = json_get_string((char *)json_text, "name");
            add_client(sockfd, id ? id : "", name ? name : "");
            if (id) { ESP_LOGI(TAG, "WS join: %s", id); }
            if (id) free(id);
            if (name) free(name);
            broadcast_peers();
            g_diag.ws_ok = true;
        } else if (strcmp(type, "chat") == 0) {
            char *to = json_get_string((char *)json_text, "to");
            if (to) {
                client_t *c = find_client_by_id(to);
                if (c) {
                    size_t l = strlen((char *)json_text);
                    uint8_t *copy = (uint8_t *)malloc(l);
                    memcpy(copy, json_text, l);
                    httpd_ws_frame_t f = (httpd_ws_frame_t){0};
                    f.type = HTTPD_WS_TYPE_TEXT;
                    f.len = l;
                    f.payload = copy;
                    httpd_ws_send_data_async(server, c->sockfd, &f, ws_free_cb, copy);
                }
                free(to);
            } else {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].sockfd) {
                        size_t l = strlen((char *)json_text);
                        uint8_t *copy = (uint8_t *)malloc(l);
                        memcpy(copy, json_text, l);
                        httpd_ws_frame_t f = (httpd_ws_frame_t){0};
                        f.type = HTTPD_WS_TYPE_TEXT;
                        f.len = l;
                        f.payload = copy;
                        httpd_ws_send_data_async(server, clients[i].sockfd, &f, ws_free_cb, copy);
                    }
                }
            }
        } else if (strcmp(type, "signal") == 0) {
            char *to = json_get_string((char *)json_text, "to");
            if (to) {
                client_t *c = find_client_by_id(to);
                if (c) {
                    size_t l = strlen((char *)json_text);
                    uint8_t *copy = (uint8_t *)malloc(l);
                    memcpy(copy, json_text, l);
                    httpd_ws_frame_t f = (httpd_ws_frame_t){0};
                    f.type = HTTPD_WS_TYPE_TEXT;
                    f.len = l;
                    f.payload = copy;
                    httpd_ws_send_data_async(server, c->sockfd, &f, ws_free_cb, copy);
                }
                free(to);
            }
        } else if (strcmp(type, "media") == 0) {
            bool a=false,v=false;
            if (json_get_bool((char *)json_text, "audio", &a)) g_diag.audio_ok = a;
            if (json_get_bool((char *)json_text, "video", &v)) g_diag.camera_ok = v;
        }
        free(type);
    }
    free(json_text);
    return ESP_OK;
}


static esp_err_t file_get_handler(httpd_req_t *req) {
    char filepath[256];
    const char *uri = req->uri;
    if (strcmp(uri, "/") == 0) uri = "/index.html";
    size_t base_len = 7; /* length of "/spiffs" */
    memcpy(filepath, "/spiffs", base_len);
    size_t ulen = strlen(uri);
    if (ulen > sizeof(filepath) - base_len - 1) ulen = sizeof(filepath) - base_len - 1;
    memcpy(filepath + base_len, uri, ulen);
    filepath[base_len + ulen] = 0;
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, "", 0);
        return ESP_OK;
    }
    if (strstr(uri, ".html")) httpd_resp_set_type(req, "text/html");
    else if (strstr(uri, ".js")) httpd_resp_set_type(req, "application/javascript");
    else if (strstr(uri, ".css")) httpd_resp_set_type(req, "text/css");
    char buf[1024];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
        httpd_resp_send_chunk(req, buf, r);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t diag_handler(httpd_req_t *req) {
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\"wifi_ap_ok\":%s,\"https_ok\":%s,\"spiffs_ok\":%s,\"ws_ok\":%s,\"camera_ok\":%s,\"audio_ok\":%s}",
        g_diag.wifi_ap_ok ? "true" : "false",
        g_diag.https_ok ? "true" : "false",
        g_diag.spiffs_ok ? "true" : "false",
        g_diag.ws_ok ? "true" : "false",
        g_diag.camera_ok ? "true" : "false",
        g_diag.audio_ok ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

static esp_err_t camera_mjpeg_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    ESP_LOGI(TAG, "MJPEG client connected");
    while (1) {
        const char *hdr = "--frame\r\nContent-Type: image/jpeg\r\n\r\n";
        if (httpd_resp_send_chunk(req, hdr, strlen(hdr)) != ESP_OK) break;
        // Placeholder frame
        const char *jpeg = "\xFF\xD8\xFF\xD9";
        if (httpd_resp_send_chunk(req, jpeg, 4) != ESP_OK) break;
        if (httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return ESP_OK;
}

static esp_err_t audio_ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        int sockfd = httpd_req_to_sockfd(req);
        for (int i = 0; i < MAX_CLIENTS; i++) if (audio_clients[i] == sockfd) return ESP_OK;
        for (int i = 0; i < MAX_CLIENTS; i++) if (audio_clients[i] == 0) { audio_clients[i] = sockfd; break; }
        if (!audio_task_running) audio_task_running = true;
        g_diag.audio_ok = true;
        ESP_LOGI(TAG, "Audio WS client registered");
        return ESP_OK;
    }
    return ESP_OK;
}

static void audio_stream_task(void *arg) {
    uint8_t buf[512];
    while (1) {
        memset(buf, 0, sizeof(buf));
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (audio_clients[i]) {
                httpd_ws_frame_t f = {0};
                f.type = HTTPD_WS_TYPE_BINARY;
                f.len = sizeof(buf);
                f.payload = buf;
                httpd_ws_send_frame_async(server, audio_clients[i], &f);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void start_https_server() {
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    extern const uint8_t servercert_pem_start[] asm("_binary_servercert_pem_start");
    extern const uint8_t servercert_pem_end[]   asm("_binary_servercert_pem_end");
    extern const uint8_t prvtkey_pem_start[]    asm("_binary_prvtkey_pem_start");
    extern const uint8_t prvtkey_pem_end[]      asm("_binary_prvtkey_pem_end");
    conf.servercert = servercert_pem_start;
    conf.servercert_len = (size_t)(servercert_pem_end - servercert_pem_start);
    conf.prvtkey_pem = prvtkey_pem_start;
    conf.prvtkey_len = (size_t)(prvtkey_pem_end - prvtkey_pem_start);
    conf.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
    conf.httpd.max_open_sockets = 12;
    conf.httpd.uri_match_fn = httpd_uri_match_wildcard;
    conf.httpd.open_fn = on_open;
    conf.httpd.close_fn = on_close;
    if (httpd_ssl_start(&server, &conf) == ESP_OK) {
        httpd_uri_t ws = {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true, .handle_ws_control_frames = true, .supported_subprotocol = ""};
        httpd_register_uri_handler(server, &ws);
        httpd_uri_t rooth = {.uri = "/", .method = HTTP_GET, .handler = file_get_handler};
        httpd_register_uri_handler(server, &rooth);
        httpd_uri_t indexh = {.uri = "/index.html", .method = HTTP_GET, .handler = file_get_handler};
        httpd_register_uri_handler(server, &indexh);
        httpd_uri_t jsh = {.uri = "/app.js", .method = HTTP_GET, .handler = file_get_handler};
        httpd_register_uri_handler(server, &jsh);
        httpd_uri_t cssh = {.uri = "/styles.css", .method = HTTP_GET, .handler = file_get_handler};
        httpd_register_uri_handler(server, &cssh);
        httpd_uri_t diag = {.uri = "/diag", .method = HTTP_GET, .handler = diag_handler};
        httpd_register_uri_handler(server, &diag);
        httpd_uri_t cam = {.uri = "/camera.mjpeg", .method = HTTP_GET, .handler = camera_mjpeg_handler};
        httpd_register_uri_handler(server, &cam);
        httpd_uri_t audio = {.uri = "/audio.ws", .method = HTTP_GET, .handler = audio_ws_handler, .is_websocket = true};
        httpd_register_uri_handler(server, &audio);
        g_diag.https_ok = true;
        ESP_LOGI(TAG, "HTTPS server started");
    }
}

static void start_http_server() {
    httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
    conf.server_port = 80;
    conf.max_open_sockets = 6;
    if (httpd_start(&server_http, &conf) == ESP_OK) {
        esp_err_t redirect(httpd_req_t *req) {
            httpd_resp_set_status(req, "301 Moved Permanently");
            httpd_resp_set_hdr(req, "Location", "https://192.168.4.1/");
            httpd_resp_send(req, "", 0);
            return ESP_OK;
        }
        httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = redirect};
        httpd_register_uri_handler(server_http, &root);
        httpd_uri_t any = {.uri = "*", .method = HTTP_GET, .handler = redirect};
        httpd_register_uri_handler(server_http, &any);
        ESP_LOGI(TAG, "HTTP redirect server on port 80 -> https://192.168.4.1/");
    }
}

static void wifi_init_ap() {
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.ap.ssid, "ESP32S3-RTC");
    strcpy((char *)wifi_config.ap.password, "esp32s3pass");
    wifi_config.ap.ssid_len = strlen("ESP32S3-RTC");
    wifi_config.ap.channel = 6;
    wifi_config.ap.max_connection = 10;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();
    g_diag.wifi_ap_ok = true;
    ESP_LOGI(TAG, "WiFi AP started: SSID ESP32S3-RTC");
}

void app_main() {
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_vfs_spiffs_conf_t conf = {.base_path = "/spiffs", .partition_label = "spiffs", .max_files = 8, .format_if_mount_failed = true};
    if (esp_vfs_spiffs_register(&conf) == ESP_OK) g_diag.spiffs_ok = true;
    if (g_diag.spiffs_ok) ESP_LOGI(TAG, "SPIFFS mounted");
    wifi_init_ap();
    start_https_server();
    start_http_server();
    xTaskCreate(audio_stream_task, "audio_stream", 4096, NULL, 5, NULL);
}