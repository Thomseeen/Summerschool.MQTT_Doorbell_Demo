/*****************************
 * Defines for camera library
 * ***************************/
// from Kconfig
#define CONFIG_XCLK_FREQ 20000000
#define CONFIG_D0 4
#define CONFIG_D1 5
#define CONFIG_D2 18
#define CONFIG_D3 19
#define CONFIG_D4 36
#define CONFIG_D5 39
#define CONFIG_D6 34
#define CONFIG_D7 35
#define CONFIG_XCLK 21
#define CONFIG_PCLK 22
#define CONFIG_VSYNC 25
#define CONFIG_HREF 23
#define CONFIG_SDA 26
#define CONFIG_SCL 27
#define CONFIG_RESET 32

// from sdkconfig.defaults
#define CONFIG_OV2640_SUPPORT 1
#define CONFIG_ESP32_DEFAULT_CPU_FREQ_240 1
#define CONFIG_MEMMAP_SMP 1
#define CONFIG_FREERTOS_UNICORE 0
#define CONFIG_FREERTOS_HZ 100

/*****************************
 * Defines for mqtt library
 * ***************************/
// from sdkconfig
#define CONFIG_ESP_MQTT_ENABLED 1
#define CONFIG_ESP_MQTT_EVENT_QUEUE_SIZE 5
#define CONFIG_ESP_MQTT_TASK_STACK_SIZE 2048
#define CONFIG_ESP_MQTT_TASK_STACK_PRIORITY 5