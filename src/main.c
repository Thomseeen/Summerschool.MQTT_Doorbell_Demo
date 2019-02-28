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
// - #define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
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
// clang-format off
// GPIO pins
#define PIN_STATUSLED   2
#define PIN_PUSHBUTTON  33

// MQTT Client-ID
#define ROOM "010"
#define CLIENTID_MQTT   "ESP32Doorbell" ROOM
#define TOPIC_MQTT_PIC  "hska/office"   ROOM "/doorbell/picture"
#define TOPIC_MQTT_TS   "hska/office"   ROOM "/doorbell/timestamp"

// TAG for the esp_log macros
#define TAG "MQTT_Doorbell"

// Expected max. size of frame - used for the send-buffer to optimize heap usage
// (camera often allocates a larger framebuffer than actually necessary)
#define MAXSIZE_OF_FRAME 27000  // bytes

// clang-format on
/*****************************************
 * Eventgroups
 *****************************************/
// Connection
static EventGroupHandle_t connection_event_group;
const static int CONNECTED_BIT_WIFI = BIT0;
const static int CONNECTED_BIT_MQTT = BIT1;
const static int RECONNECT_BIT_MQTT = BIT2;

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

// Reconnect MQTT without init only if actually needed and not already triggered by mqtt_init
void mqtt_reconnect() {
    // Wait for a Wifi-connection
    if (RECONNECT_BIT_MQTT & xEventGroupGetBits(connection_event_group)) {
        esp_mqtt_start(CONFIG_MQTT_BROKER_IP, CONFIG_MQTT_PORT, CLIENTID_MQTT, CONFIG_MQTT_USER, CONFIG_MQTT_PASS);
        xEventGroupClearBits(connection_event_group, RECONNECT_BIT_MQTT);
    }
}

// Reconnect MQTT with init (definition at init functions)
/*****************************************
 * Task functions
 *****************************************/
// Task to blink, turn on or turn off the status LED
void statusled_task(void* pvParameter) {
    // Setup the GPIO pin
    gpio_pad_select_gpio(PIN_STATUSLED);
    gpio_set_direction(PIN_STATUSLED, GPIO_MODE_INPUT_OUTPUT);
    // Tasks "main"-loop
    while (1) {
        // Use eventgroup to blink, turn on or turn off the status LED based on WiFi and MQTT connection
        if (CONNECTED_BIT_MQTT & xEventGroupGetBits(connection_event_group)) {
            gpio_set_level(PIN_STATUSLED, 1);
        } else if (CONNECTED_BIT_WIFI & xEventGroupGetBits(connection_event_group)) {
            gpio_set_level(PIN_STATUSLED, !gpio_get_level(PIN_STATUSLED));
        } else {
            gpio_set_level(PIN_STATUSLED, 0);
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
            // Build timestamp buffer
            uint8_t send_buffer_time[4];
            // The time_t datatype is a long -> 4 bytes that have to be sent
            send_buffer_time[0] = (uint8_t)(now >> 24) & 0xFF;
            send_buffer_time[1] = (uint8_t)(now >> 16) & 0xFF;
            send_buffer_time[2] = (uint8_t)(now >> 8) & 0xFF;
            send_buffer_time[3] = (uint8_t)now & 0xFF;
            // Send time stamp
            esp_mqtt_publish(TOPIC_MQTT_TS, send_buffer_time, 4, 1, true);
            // Check RAM
            ESP_LOGI(TAG, "Biggest free heap-block is %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));  // heapcontrol
            // Send picture
            esp_mqtt_publish(TOPIC_MQTT_PIC, fb->buf, fb->len, 1, true);
            // Give back the buffer pointer
            esp_camera_fb_return(fb);
            // Debounce
            vTaskDelay(500 / portTICK_PERIOD_MS);
        } else {
            // Polling the button every 100ms
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
}

/*****************************************
 * Event handler and callbacks
 *****************************************/
// Wifi event handler
static esp_err_t wifi_event_handler(void* ctx, system_event_t* event) {
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            // WiFi got initialized
            ESP_ERROR_CHECK(esp_wifi_connect());
            ESP_LOGI(TAG, "Wifi connecting...");
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            // WiFi got connected; DHCP client started
            ESP_LOGI(TAG, "Wifi connected...");
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            // WiFi got connected and DHCP retrieved an IP
            xEventGroupSetBits(connection_event_group, CONNECTED_BIT_WIFI);
            ESP_LOGI(TAG, "Wifi got IP.");
            mqtt_reconnect();
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            xEventGroupClearBits(connection_event_group, CONNECTED_BIT_MQTT | CONNECTED_BIT_WIFI);
            ESP_LOGI(TAG, "Wifi disconnected");
            // WiFi disconnected
            esp_mqtt_stop();
            // Try to reastablish a Wifi-connection
            if (!(CONNECTED_BIT_WIFI & xEventGroupGetBits(connection_event_group))) {
                ESP_ERROR_CHECK(esp_wifi_connect());
                ESP_LOGI(TAG, "Wifi trying to reconnect");
            }
            xEventGroupSetBits(connection_event_group, RECONNECT_BIT_MQTT);
            break;
        default:
            ESP_LOGW(TAG, "unknown WiFi-state");
            xEventGroupClearBits(connection_event_group, CONNECTED_BIT_MQTT | CONNECTED_BIT_WIFI);
            break;
    }
    return ESP_OK;
}

// MQTT status callback
void mqtt_status_callback(esp_mqtt_status_t status) {
    static TaskHandle_t mqtt_publish_task_handle = NULL;
    switch (status) {
        case ESP_MQTT_STATUS_CONNECTED:
            // Connected to MQTT-Broker
            // Task to publish data on button-down should be created and started if this is a fresh startup
            // and should be resumed if it has already been created
            xEventGroupSetBits(connection_event_group, CONNECTED_BIT_MQTT);
            if (!mqtt_publish_task_handle) {
                ESP_LOGI(TAG, "MQTT connected - starting publish task");
                if (xTaskCreate(mqtt_publish_task, "mqtt_publish_task", 4096, NULL, 10, &mqtt_publish_task_handle) != pdPASS) {
                    ESP_LOGE(TAG, "mqtt_publish_task could not be created");
                }
            } else {
                ESP_LOGI(TAG, "MQTT connected - resuming publish task");
                vTaskResume(mqtt_publish_task_handle);
            }
            break;
        case ESP_MQTT_STATUS_DISCONNECTED:
            xEventGroupClearBits(connection_event_group, CONNECTED_BIT_MQTT);
            // Disconnected from MQTT-Broker
            ESP_LOGI(TAG, "MQTT disconnected - suspending publish task");
            // Task to publish data on button-down can be suspended as long as there is no connection to a broker
            vTaskSuspend(mqtt_publish_task_handle);
            ESP_LOGI(TAG, "Biggest free heap-block is %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));  // heapcontrol
            // When working at the limit of the RAM-size there might be the need to wait for the IDLE-task to free memory
            // ESP_LOGI(TAG, "Let IDLE-Task free memory");
            // vTaskDelay(5000 / portTICK_PERIOD_MS);
            // ESP_LOGI(TAG, "Biggest free heap-block is %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));  // heapcontrol
            // Try to reastablish a connection to the MQTT-Broker
            xEventGroupSetBits(connection_event_group, RECONNECT_BIT_MQTT);
            mqtt_reconnect();
            break;
    }
}

/*****************************************
 * Init / Start functions
 *****************************************/
// Wifi
void wifi_init() {
    ESP_LOGI(TAG, "Initializing Wifi");
    tcpip_adapter_init();
    connection_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta =
            {
                // clang-format off
                .ssid       = CONFIG_WIFI_SSID,
                .password   = CONFIG_WIFI_PASSWORD,
                // clang-format on
            },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// SNTP
void sntp_start() {
    // Wait for a Wifi-connection
    xEventGroupWaitBits(connection_event_group, CONNECTED_BIT_WIFI, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, CONFIG_SERVER_NTP);
    sntp_init();
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;
    // If the time has not been set yet the time will be 0 which is 1.1.1970 0:0:0.0
    // tm_year of the tm struct is in years since 1900
    while ((timeinfo.tm_year <= (1970 - 1900)) && (++retry < retry_count)) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

// Camera
void cam_init() {
    ESP_LOGI(TAG, "Initializing camera");
    camera_config_t camera_config = {
        // clang-format off
        .ledc_channel   = LEDC_CHANNEL_0,
        .ledc_timer     = LEDC_TIMER_0,
        .pin_d0         = CONFIG_D0,
        .pin_d1         = CONFIG_D1,
        .pin_d2         = CONFIG_D2,
        .pin_d3         = CONFIG_D3,
        .pin_d4         = CONFIG_D4,
        .pin_d5         = CONFIG_D5,
        .pin_d6         = CONFIG_D6,
        .pin_d7         = CONFIG_D7,
        .pin_xclk       = CONFIG_XCLK,
        .pin_pclk       = CONFIG_PCLK,
        .pin_vsync      = CONFIG_VSYNC,
        .pin_href       = CONFIG_HREF,
        .pin_sscb_sda   = CONFIG_SDA,
        .pin_sscb_scl   = CONFIG_SCL,
        .pin_reset      = CONFIG_RESET,
        .xclk_freq_hz   = CONFIG_XCLK_FREQ,
        .pixel_format   = PIXFORMAT_JPEG,
        .frame_size     = FRAMESIZE_VGA,
        .jpeg_quality   = 30,
        .fb_count       = 1,
        // clang-format on
    };
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }
}

// MQTT
void mqtt_init() {
    // Wait for a Wifi-connection
    xEventGroupWaitBits(connection_event_group, CONNECTED_BIT_WIFI, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Initializing MQTT");
    esp_mqtt_init(mqtt_status_callback, NULL, MAXSIZE_OF_FRAME, 30000);
    esp_mqtt_start(CONFIG_MQTT_BROKER_IP, CONFIG_MQTT_PORT, CLIENTID_MQTT, CONFIG_MQTT_USER, CONFIG_MQTT_PASS);
}
/*****************************************
 * Main
 *****************************************/
void app_main() {
    // Set log levels
    /*
     *esp_log_level_set("heap_init", ESP_LOG_INFO);
     *esp_log_level_set("intr_alloc", ESP_LOG_INFO);
     *esp_log_level_set("esp_dbg_stubs", ESP_LOG_INFO);
     *esp_log_level_set("wifi", ESP_LOG_INFO);
     *esp_log_level_set("nvs", ESP_LOG_INFO);
     *esp_log_level_set("system_api", ESP_LOG_WARN);
     *esp_log_level_set("gpio", ESP_LOG_WARN);
     *esp_log_level_set("phy_init", ESP_LOG_INFO);
     *esp_log_level_set("tcpip_adapter", ESP_LOG_INFO);
     *esp_log_level_set("ledc", ESP_LOG_INFO);
     *esp_log_level_set("camera", ESP_LOG_INFO);
     *esp_log_level_set("esp_mqtt", ESP_LOG_INFO);
     */

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
    ESP_LOGI(TAG, "The current date/time in Karlsruhe is: %s", timestr_buffer);
    // Camera init
    cam_init();
    // MQTT init
    mqtt_init();
}