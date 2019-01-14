/********************************
 * Communication relevant defines
 * ******************************/
#define CONFIG_WIFI_SSID "MQTT-TestBroker"
#define CONFIG_WIFI_PASSWORD "MQTT-TestBroker"
#define CONFIG_SERVER_IP "192.168.178.2"
#define CONFIG_MQTT_BROKER_IP CONFIG_SERVER_IP
#define CONFIG_MQTT_USER NULL
#define CONFIG_MQTT_PASS NULL
#define CONFIG_MQTT_PORT "1883"
#define CONFIG_SERVER_NTP \
    CONFIG_SERVER_IP  // 0x02B2A8C0 - own laptop in the test-wlan NTP-Server
                      //#define CONFIG_SERVER_NTP 0x01B2A8C0  // 192.168.178.1 - fritz.box NTP-Server
                      //#define CONFIG_SERVER_NTP 0x1341C4C1  // 193.196.65.19 - HS-NTP-Server