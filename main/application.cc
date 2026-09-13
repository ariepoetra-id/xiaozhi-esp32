#include "application.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <esp_http_client.h>
#include <cJSON.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "audio_codecs/audio_codec.h"
#include "display.h"
#include "system_info.h"
#include "assets.h"

#define TAG "Application"
#define YT_TAG "YouTubeMusic"

// Fungsi ACK untuk konfirmasi ke server setelah perintah diproses
static void SendMusicAck(const char* command_id, const char* device_id) {
    char url[256];
    snprintf(url, sizeof(url), "https://xiaozhiscig.biz.id/api/audio/ack/%s", command_id);

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_perform(client);
        esp_http_client_cleanup(client);
    }
}

// Struktur untuk menampung respons HTTP GET dari server
struct HttpBuffer {
    char* memory;
    size_t size;
};

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    HttpBuffer *buffer = (HttpBuffer *)evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                buffer->memory = (char *)realloc(buffer->memory, buffer->size + evt->data_len + 1);
                if (buffer->memory) {
                    memcpy(&(buffer->memory[buffer->size]), evt->data, evt->data_len);
                    buffer->size += evt->data_len;
                    buffer->memory[buffer->size] = 0;
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Task Background Polling YouTube Music
static void youtube_music_poll_task(void *pvParameter) {
    char device_id[64] = "esp32-c3-supermini";

    while (true) {
        char url[256];
        snprintf(url, sizeof(url), "https://xiaozhiscig.biz.id/api/audio/commands/%s", device_id);

        HttpBuffer resp_buffer = {0};
        resp_buffer.memory = (char *)malloc(1);
        resp_buffer.size = 0;

        esp_http_client_config_t config = {};
        config.url = url;
        config.method = HTTP_METHOD_GET;
        config.timeout_ms = 5000;
        config.event_handler = http_event_handler;
        config.user_data = &resp_buffer;

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client != NULL) {
            esp_err_t err = esp_http_client_perform(client);
            if (err == ESP_OK && esp_http_client_get_status_code(client) == 200) {
                if (resp_buffer.size > 0) {
                    cJSON *root = cJSON_Parse(resp_buffer.memory);
                    if (root != NULL) {
                        cJSON *command_id_json = cJSON_GetObjectItem(root, "command_id");
                        cJSON *audio_url_json = cJSON_GetObjectItem(root, "audio_url");

                        if (cJSON_IsString(command_id_json) && cJSON_IsString(audio_url_json)) {
                            const char* command_id = command_id_json->valuestring;
                            const char* audio_url = audio_url_json->valuestring;

                            ESP_LOGI(YT_TAG, "Menerima perintah musik. ID: %s, URL: %s", command_id, audio_url);

                            // Kirim konfirmasi (ACK) ke server
                            SendMusicAck(command_id, device_id);
                        }
                        cJSON_Delete(root);
                    }
                }
            }
            esp_http_client_cleanup(client);
        }

        if (resp_buffer.memory != NULL) {
            free(resp_buffer.memory);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

Application::Application() {
}

Application::~Application() {
}

Application& Application::GetInstance() {
    static Application instance;
    return instance;
}

void Application::Init() {
    // Inisialisasi utama sistem bawaan repo
    Board& board = Board::GetInstance();
    
    ESP_LOGI(TAG, "Starting application initialization");

    // Jalankan task polling YouTube Music di background
    xTaskCreate(youtube_music_poll_task, "yt_music_task", 4096, NULL, 3, NULL);
}

void Application::SetDeviceState(DeviceState state) {
    device_state_ = state;
}

DeviceState Application::GetDeviceState() const {
    return device_state_;
}
