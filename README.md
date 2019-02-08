# Summerschool.MQTT_Doorbell_Demo
An ESP32 ESP-IDF project in PlatformIO using an OV2640 camera to send a picture via MQTT to a broker on a push-button signal: A Doorbell.

# Hardware
This project uses an ESP32 Devkit 1 board and an OV2640 breakout board. Also a push-button is used to "ring the bell". The button is wired up so it's low-active and connected to D33 on the ESP32. In the default configuration the camera needs to be wired up as shown below:

| Interface| Camera PIN| ESP32 Pin| Code define
| ---| ---| ---| --- 
| SCCB Clock| SIOC| 27| CONFIG_SCL
| SCCB Data| SIOD| 26| CONFIG_SDA
| System Clock| XCLK| 21| CONFIG_XCLK
| Vertical Sync| VSYNC| 25| CONFIG_VSYNC
| Horizontal Reference| HREF| 23| CONFIG_HREF
| Pixel Clock| PCLK| 22| CONFIG_PCLK
| Pixel Data Bit 0| D2| 4| CONFIG_D0
| Pixel Data Bit 1| D3| 5| CONFIG_D1
| Pixel Data Bit 2| D4| 18| CONFIG_D2
| Pixel Data Bit 3| D5| 19| CONFIG_D3
| Pixel Data Bit 4| D6| 36 (VP)| CONFIG_D4
| Pixel Data Bit 5| D7| 39 (VN)| CONFIG_D5
| Pixel Data Bit 6| D8| 34| CONFIG_D6
| Pixel Data Bit 7| D9| 35| CONFIG_D7
| Camera Reset| RET| 32| CONFIG_RESET
| Camera Power Down| PWDN| 100k Pulldown to Gnd| 
| Power Supply 3.3V| 3V3| 3V3| 
| Ground| GND| GND| 

# Client
To display if someone ringed the door and show the image taken, take a look at https://github.com/Thomseeen/Summerschool.MQTT_Doorbell_Client.
