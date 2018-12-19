/*****************************************
 * Includes
 *****************************************/
// Generic
#include <string.h>
#include <time.h>
// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
// ESP32 APIs and functions
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event_loop.h"
// Get lower level log messages
// #define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
// SNTP
#include "apps/sntp/sntp.h"
#include "lwip/err.h"
// Camera
#include "esp_camera.h"
// MQTT
#include "esp_mqtt.h"
// Configs
#include "comconfig.h"
#include "exlibconfig.h"

/*****************************************
 * Local defines/settings
 *****************************************/
// GPIO pins
#define PIN_WIFISTATUSLED 2
#define PIN_PUSHBUTTON 33
// MQTT Client-ID
#define CLIENTID_MQTT "ESP32Doorbell01"
#define TOPIC_MQTT "hska/office010/doorbell"
// Camera formats
#define CAMERA_PIXEL_FORMAT CAMERA_PF_GRAYSCALE
#define CAMERA_FRAME_SIZE CAMERA_FS_QVGA
// TAG for the esp_log macros
#define TAG "MQTT_Doorbell"

/*****************************************
 * Eventgroups
 *****************************************/
// Wifi
static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;
const static int CONNECTING_BIT = BIT1;

/*****************************************
 * Local helperfunctions
 *****************************************/
// Call esps's local time, set it's timezone and return a tm-struct
struct tm getLocalTime() {
    time_t now;
    struct tm timeinfo;
    setenv("TZ", "CET-1CET,M3.5.0/2,M10.5.0/3", 1);  // Central European Timezone including summer- wintertime
    tzset();
    time(&now);
    localtime_r(&now, &timeinfo);
    return timeinfo;
}

/*****************************************
 * Task functions
 *****************************************/
// Task to blink, turn on or turn off the wifi status LED
void statusled_task(void* pvParameter) {
    // Setup the GPIO pin
    gpio_pad_select_gpio(PIN_WIFISTATUSLED);
    gpio_set_direction(PIN_WIFISTATUSLED, GPIO_MODE_INPUT_OUTPUT);
    // Tasks "main"-loop
    while (1) {
        // Use wifi's eventgroup to blink, turn on or turn off the wifi status LED
        if (CONNECTED_BIT & xEventGroupGetBits(wifi_event_group)) {
            gpio_set_level(PIN_WIFISTATUSLED, 1);
        } else if (CONNECTING_BIT & xEventGroupGetBits(wifi_event_group)) {
            gpio_set_level(PIN_WIFISTATUSLED, !gpio_get_level(PIN_WIFISTATUSLED));
        } else {
            gpio_set_level(PIN_WIFISTATUSLED, 0);
        }
        // Task gets called every 200ms => reaction time < 200ms and blink frequenzy is 5Hz
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}
// Task to poll the pushbutton and publish a picture at button down
void mqtt_publish_task(void* pvParameter) {
    gpio_pad_select_gpio(PIN_PUSHBUTTON);
    gpio_set_direction(PIN_PUSHBUTTON, GPIO_MODE_INPUT);
    while (1) {
        if (!gpio_get_level(PIN_PUSHBUTTON)) {
            // Get the current time
            time_t now;
            time(&now);
            struct tm localtime = getLocalTime();
            // A C-String (char-array) to store a formatted string
            char timestr_buffer[64];
            // Build a human-readable string of the time information
            strftime(timestr_buffer, sizeof(timestr_buffer), "%c", &localtime);
            // Shoot a picture and retrieve the pointer to a struct containing the buffer
            camera_fb_t* fb = esp_camera_fb_get();
            if (!fb) {
                ESP_LOGE(TAG, "Camera Capture Failed");
                break;
            }
            ESP_LOGI(TAG, "Doorbell ringing at %s, picture with %dbytes sent", timestr_buffer, fb->len);
            // Build buffer
            uint8_t send_buffer[15366];
            // - Time
            // The time_t datatype is a long -> 4 bytes that have to be sent
            send_buffer[0] = 'L';  // (Local) Used to easily identify the time message part in a hex-dump
            send_buffer[1] = (uint8_t)(now >> 24) & 0xFF;
            send_buffer[2] = (uint8_t)(now >> 16) & 0xFF;
            send_buffer[3] = (uint8_t)(now >> 8) & 0xFF;
            send_buffer[4] = (uint8_t)now & 0xFF;
            send_buffer[5] = 'D';  // (Data) Used to easily identify the picure message part in a hex-dump
            // - Picture
            memcpy(send_buffer + 6, fb->buf, fb->len);
            // Publish
            esp_mqtt_publish(TOPIC_MQTT, send_buffer, fb->len + 6, 0, false);
            // Give back the buffer pointer
            esp_camera_fb_return(fb);
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}
/*****************************************
 * Event handler and callbacks
 *****************************************/
// Wifi
static esp_err_t wifi_event_handler(void* ctx, system_event_t* event) {
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            // WiFi got initialized
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            // WiFi got connected; DHCP client started
            xEventGroupSetBits(wifi_event_group, CONNECTING_BIT);
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            // WiFi got connected and DHCP retrieved an IP
            xEventGroupClearBits(wifi_event_group, CONNECTING_BIT);
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            // WiFi disconnected
            esp_mqtt_stop();
            // Try to reastablish a Wifi-connection
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTING_BIT | CONNECTED_BIT);
            break;
        default:
            ESP_LOGW(TAG, "unknown WiFi-state");
            xEventGroupClearBits(wifi_event_group, CONNECTING_BIT | CONNECTED_BIT);
            break;
    }
    return ESP_OK;
}
// MQTT
// - incoming msg callback
void mqtt_message_callback(const char* topic, uint8_t* payload, size_t len) {
    ESP_LOGI(TAG, "MQTT incoming msg: %s => %s (%d)", topic, payload, (int)len);
}
// - status callback
void mqtt_status_callback(esp_mqtt_status_t status) {
    static TaskHandle_t mqtt_publish_task_handle = NULL;
    switch (status) {
        case ESP_MQTT_STATUS_CONNECTED:
            // Connected to MQTT-Broker
            ESP_LOGI(TAG, "MQTT connected - starting/resuming publish task");
            // Task to publish data on button-down should be created and started if this is a fresh startup
            // and should be resumed if it has already been created
            if (!mqtt_publish_task_handle) {
                xTaskCreate(mqtt_publish_task, "mqtt_publish_task", 20048, NULL, 10, &mqtt_publish_task_handle);
            } else {
                vTaskResume(mqtt_publish_task_handle);
            }
            break;
        case ESP_MQTT_STATUS_DISCONNECTED:
            // Disconnected from MQTT-Broker
            ESP_LOGI(TAG, "MQTT disconnected - suspending publish task");
            // Task to publish data on button-down can be suspended as long as there is no connection to a broker
            vTaskSuspend(mqtt_publish_task_handle);
            // Try to reastablish a conenction to the MQTT-Broker
            esp_mqtt_start(CONFIG_MQTT_BROKER_IP, CONFIG_MQTT_PORT, CLIENTID_MQTT, CONFIG_MQTT_USER, CONFIG_MQTT_PASS);
            break;
    }
}
/*****************************************
 * Init / Start functions
 *****************************************/
// Wifi
void wifi_init(void) {
    ESP_LOGI(TAG, "Initializing Wifi");
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = CONFIG_WIFI_SSID,
                .password = CONFIG_WIFI_PASSWORD,
            },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}
// SNTP
void sntp_start() {
    // Wait for a Wifi-connection
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, CONFIG_SERVER_NTP);
    sntp_init();
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;
    // If the time has not been set yet the time will be 0 which is 1.1.1970 0:0:0.0
    // tm_year of the tm struct is inyears since 1900
    while ((timeinfo.tm_year <= (1970 - 1900)) && (++retry < retry_count)) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}
// Camera
void cam_init(void) {
    ESP_LOGI(TAG, "Initializing camera");
    camera_config_t camera_config = {
        .ledc_channel = LEDC_CHANNEL_0,
        .ledc_timer = LEDC_TIMER_0,
        .pin_d0 = CONFIG_D0,
        .pin_d1 = CONFIG_D1,
        .pin_d2 = CONFIG_D2,
        .pin_d3 = CONFIG_D3,
        .pin_d4 = CONFIG_D4,
        .pin_d5 = CONFIG_D5,
        .pin_d6 = CONFIG_D6,
        .pin_d7 = CONFIG_D7,
        .pin_xclk = CONFIG_XCLK,
        .pin_pclk = CONFIG_PCLK,
        .pin_vsync = CONFIG_VSYNC,
        .pin_href = CONFIG_HREF,
        .pin_sscb_sda = CONFIG_SDA,
        .pin_sscb_scl = CONFIG_SCL,
        .pin_reset = CONFIG_RESET,
        .xclk_freq_hz = CONFIG_XCLK_FREQ,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count = 1,
    };
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }
}
void mqtt_init(void) {
    // Wait for a Wifi-connection
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Initializing MQTT");
    esp_mqtt_init(mqtt_status_callback, mqtt_message_callback, 17000, 5000);
    esp_mqtt_start(CONFIG_MQTT_BROKER_IP, CONFIG_MQTT_PORT, CLIENTID_MQTT, CONFIG_MQTT_USER, CONFIG_MQTT_PASS);
}
/*****************************************
 * Main
 *****************************************/
void app_main() {
    // Set log levels
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("system_api", ESP_LOG_WARN);
    esp_log_level_set("gpio", ESP_LOG_WARN);
    // Initialize nvs flash
    ESP_ERROR_CHECK(nvs_flash_init());
    // WiFi init and status LED task start
    wifi_init();
    xTaskCreate(&statusled_task, "statusled_task", 1024, NULL, 1, NULL);
    // Time init
    sntp_start();
    // C-string (char array) to store a human-readable time format in it
    char timestr_buffer[64];
    // Get the current time via helperfunction
    struct tm localtime = getLocalTime();
    // Format the UNIX time into a human-readable string
    strftime(timestr_buffer, sizeof(timestr_buffer), "%c", &localtime);
    ESP_LOGI(TAG, "The current date/time in Kalrsruhe is: %s", timestr_buffer);
    // Camera init
    cam_init();
    // MQTT init
    mqtt_init();
}