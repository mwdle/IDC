#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <deque>

#define SPIFFS LittleFS

#define errorLed D0

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Only put your credentials here if you aren't storing them on the filesystem. You can learn more about this in the README.
String ssid = "";
String password = "";

IPAddress subnet(255, 255, 255, 0);
IPAddress gateway(192, 168, 0, 1);
IPAddress ip(192, 168, 0, 98);

AsyncWebServer server(80);

// Initialize display:
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool displayChangesQueued = false;

// Initialize WebSocket:
// REQUIRES WebSockets.h to have #define WEBSOCKETS_NETWORK_TYPE NETWORK_ESP8266_ASYNC OR loop() to have ws.loop();
WebSocketsServer ws = WebSocketsServer(81);
std::deque<int> newlyConnectedClients;

unsigned long lastWifiCheck = 0;

// Handles any websocket client connections/disconnections and messages.
// Any incoming message from websocket client is parsed into a pixel change and applied to the display, and is also broadcast to all clients to synchronize canvas state.
// Any new clients are added to a queue to be sent the current canvas state.
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED:
    {
      IPAddress ip = ws.remoteIP(num);
      Serial.printf("Client:[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      newlyConnectedClients.push_back(num);
    }
    break;
    case WStype_TEXT:
    {
      ws.broadcastTXT(payload, length);

      JsonDocument msg;
      deserializeJson(msg, payload);

      int clearFlag = msg["clearFlag"];
      if (clearFlag) display.fillRect(0, 0, 128, 64, BLACK);
      else {
        int color = msg["color"]; 
        int x = msg["x"]; 
        int y = msg["y"];
        int size = msg["cellSize"];
        display.fillRect(x, y, size, size, color);
      }
      displayChangesQueued = true;
    }
    break;
  }
}

void setup(void) {
  // Initialize LED Pina
  pinMode(errorLed, OUTPUT);

  Serial.begin(115200);

  // Initialize Filesystem and read Wi-fi credentials from file.
  if (!LittleFS.begin()) {
    Serial.println("\nAn error has occurred while mounting FS");
  }
  else{
    Serial.println("\nFS mounted successfully");
  }
  File file = LittleFS.open("secret", "r");
  if (file) {
    ssid = file.readStringUntil('\n');
    password = file.readStringUntil('\n');
    file.close();
  }

  // Initialize Wi-Fi
  WiFi.config(ip, gateway, subnet, gateway);
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(ssid.c_str(), password.c_str());
  while(WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(errorLed, LOW);   // Turn on the red onboard LED by making the voltage LOW
    delay(500);                    
    digitalWrite(errorLed, HIGH);  // Turn off the red onboard LED by making the voltage HIGH
    delay(800);
  }
  digitalWrite(errorLed, HIGH);
  Serial.printf("Wifi Connected! IP Address: %s\n", WiFi.localIP().toString().c_str());

  // Initialize OTA & Websocket servers.
  ElegantOTA.begin(&server);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.serveStatic("/", LittleFS, "/");
  ws.begin();
  ws.onEvent(webSocketEvent);
  server.begin();

  // Initialize display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println("Display intialization failed");
  }
  delay(2000);
  display.clearDisplay();
  displayChangesQueued = true;
}

void loop(void) {
  ElegantOTA.loop();

  if(millis() - lastWifiCheck > 3000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) digitalWrite(errorLed, LOW); // Turn on red onboard LED by setting voltage LOW            
    else digitalWrite(errorLed, HIGH); // Turn off red onboard LED by setting voltage HIGH
  }

  if (displayChangesQueued) {
    displayChangesQueued = false;
    display.display();
  }

  // If a new client has recently connected, send them a binary representation of the current canvas state.
  if (!newlyConnectedClients.empty()) {
    int numBytes = (SCREEN_WIDTH * SCREEN_HEIGHT + 7) / 8;
    std::vector<uint8_t> binaryData(numBytes, 0);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
      for (int x = 0; x < SCREEN_WIDTH; x++) {
        int byteIndex = (y * SCREEN_WIDTH + x) / 8;
        int bitIndex = 7 - (y * SCREEN_WIDTH + x) % 8;
        binaryData[byteIndex] |= (display.getPixel(x,y) << bitIndex);
      }
    }
    ws.sendBIN(newlyConnectedClients.front(), binaryData.data(), binaryData.size());
    newlyConnectedClients.pop_front();
  }
}