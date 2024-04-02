# I2C Canvas Controller
Web-based drawing canvas mirrored onto an I2C display in realtime using an ESP8266 NodeMCU board.    <br><br>    
<p align="center" float="left">
  <img src="webPage.gif" alt="animated" width="45%"/>
  <img src="display.gif" alt="animated" width="45%"/>
</p>        

## Functionality:
* Webserver with interactive pixel canvas that mirrors all input to attached I2C displays (http://\<espIP\>:443).
  * Users are able draw, erase, change brush size, and clear the canvas.
* Internet Serial Interface - includes WebSerial library to allow serial monitoring and input over Wi-Fi (http://\<espIP\>/webserial).
* Internet based controls via WebSerial input:
    * /setPixel x y \<status\> (where \<status\> should be replaced with either "on" or "off")
    * /restart - restarts the ESP device.
* Internet connection status indicator: Red onboard LED will turn on anytime the ESP is not connected to a Wi-Fi network.
* OTA Updates - includes ElegantOTA library to allow for Over-The-Air firmware and filesystem updates (http://\<espIP\>/update).    <br><br>

# Specifications and Pinout
All builds were created and tested using the PlatformIO IDE extension for VSCode and Espressif ESP8266 NodeMCU board paired with a 2 pin .96 Inch 128x64 I2C SSD1306 OLED display. Mileage may vary using other boards, IDE's, and displays.    <br><br>    

The following libraries/dependencies are required (for basic and network functions):
* [ICCWebPage](https://github.com/mwdle/ICCWebPage)
* [Elegant OTA](https://github.com/ayushsharma82/ElegantOTA)
* [WebSerialDark](https://github.com/mwdle/WebSerialDark)
* [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)    
* [Adafruit-GFX-Library](https://github.com/adafruit/Adafruit-GFX-Library)    
* [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)    <br><br>

## Network Info:
* This program requires internet to function
* The recommended way of storing your credentials is in a file named "secret" that must be placed in the data folder of this project, and built to the filesystem as described in the filesystem section below.
  * The format of the secret file is ssid directly followed by a newline followed by password followed by another newline.
  * Ensure you secret file uses LF (\\n) EOL sequence instead of CRLF (\\r\\n), otherwise the program will be unable to properly parse your Wi-Fi Credentials.
* If you choose not to store your credentials in a file, you can simply set the main.cpp variables "ssid" and "password" accordingly.
* You may also need to change the subnet, gateway, and local_ip variables.    <br><br>

## Filesystem and Webpage Info:
* The webpage resources required by this project must be uploaded to your microcontroller's filesystem independently of the compiled source code / firmware.
* The filesystem used for this project is LittleFS (as opposed to the deprecated SPIFFS).
* To upload the webpage resources to your microcontroller, in Arduino or PlatformIO, use the tool to build and upload the filesystem image. After doing so, you may build and upload the firmware.    
* The webpage source code in this project has been minified to save space. To make any modifications to it, feel free to download the raw (unminified) code from https://github.com/mwdle/ICCWebPage<br><br>

## Pinout Info:
* errorLED is the pin of your onboard red LED. This program uses pin D0 (16) on the ESP8266 NodeMCU.    
* The default display pins (SCL and SDA) default to pins D1 and D2, respectively, on the ESP8266. However, these defaults can be overriden by adding the following to the setup(): Wire.begin(sda, scl);    <br><br>
