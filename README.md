# I2C Display Controller
Stateful multi-client web-based drawing canvas mirrored onto an I2C display and clients' canvases in realtime using an ESP8266 NodeMCU board.    <br><br>    
<p align="center" float="left">
  <img src="webPage.gif" alt="animated" width="45%"/>
  <img src="display.gif" alt="animated" width="45%"/>
</p>        

## Functionality:
* Webserver with interactive and stateful pixel canvas that mirrors input to attached I2C displays and all webserver clients in realtime (http://\<espIP\>).
  * Users are able draw, erase, change brush size, clear the canvas, upload images to the canvas (via file selector or drag and drop), and download the canvas image.
  * Users are able to create and delete canvases on the server, and rotate between available canvases.
    * The currently selected canvas autosaves to a file every one second.
  * Uploaded images are automatically downscaled to 128x64 and converted to black and white. A few sample images to upload are available in the sampleImages folder.
    * Due to the limited physical display size, large and/or complex images may not appear as expected after processing.
    * For best results, upload images that are already black and white and/or a 2:1 aspect ratio (ideally 128x64 pixels).
* Internet connection status indicator: Red onboard LED will illuminate whenever the ESP is not connected to a Wi-Fi network.
* OTA Updates - includes ElegantOTA library to allow for Over-The-Air firmware and filesystem updates (http://\<espIP\>/update).

## Specifications and things you should know
All builds were created and tested using the PlatformIO IDE extension for VSCode and Espressif ESP8266 NodeMCU board paired with a 2 pin .96 Inch 128x64 I2C SSD1306 OLED display. Mileage may vary using other boards, IDE's, and displays.    <br><br>

The following libraries/dependencies are required (for basic and network functions):
* [Elegant OTA](https://github.com/ayushsharma82/ElegantOTA)
* [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)    
* [Adafruit-GFX-Library](https://github.com/adafruit/Adafruit-GFX-Library)    
* [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
* [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
* [ArduinoWebSockets](https://github.com/Links2004/arduinoWebSockets)
* [ESP8266WiFi](https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi)
* [ESPAsyncTCP](https://github.com/me-no-dev/ESPAsyncTCP)
* [ESP8266 LittleFS Wrapper](https://github.com/esp8266/Arduino/blob/master/libraries/LittleFS/src/LittleFS.h)    <br><br>

The following line must be uncommented in the WebSockets.h file of the ArduinoWebSockets library before building and uploading the project to your microcontroller:
* #define WEBSOCKETS_NETWORK_TYPE NETWORK_ESP8266_ASYNC

ALTERNATIVELY - Add the following line to the loop() function in main.cpp (performance will be slower than using the ASYNC websocket network type):
* ws.loop();

### Network Info:
* This program requires internet to function
* You may need to change the subnet, gateway, and local_ip variables.    
* The recommended way of storing your Wi-Fi credentials is in a file named "secret" that must be placed in the data folder of this project, and loaded onto the filesystem as described in the filesystem section below.
  * The format of the secret file is ssid directly followed by a newline followed by password followed by another newline.
  * Ensure you secret file uses LF (\\n) EOL sequence instead of CRLF (\\r\\n), otherwise the program will be unable to properly parse your Wi-Fi Credentials.
* If you choose not to store your credentials in a file, you can simply set the main.cpp variables "ssid" and "password" accordingly.

### Filesystem and Webpage Info:
* The webpage resources required by this project must be uploaded to your microcontroller's filesystem independently of the compiled source code / firmware.
* To upload the webpage resources to your microcontroller, in Arduino or PlatformIO, use the tool to build and upload the filesystem image. After doing so, you may build and upload the firmware.    
* The filesystem used for this project is LittleFS (as opposed to the deprecated SPIFFS).

### Pinout Info:
* errorLED is the pin of your onboard red LED. This program uses pin D0 (16) on the ESP8266 NodeMCU.    
* The display pins: SCL and SDA, default to pins D1 and D2, respectively, on the ESP8266. However, these defaults can be overriden by adding the following to the setup(): Wire.begin(sda, scl);

### License
* This project is licensed under the GPL 3.0 license. However, please note that the images included in this repository are not covered by this license. For more information, see the `IMAGES_LICENSE.txt` file.