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
std::deque<int> clientsNeedingCanvas;

unsigned long lastWifiCheck = 0;

// Applies data from a binary image file to the display.
void applyBinaryToDisplay(uint8_t* buf) {
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      int byteIndex = (y * SCREEN_WIDTH + x) / 8;
      int bitIndex = 7 - (y * SCREEN_WIDTH + x) % 8;
      int color = (buf[byteIndex] >> bitIndex) & 1;
      display.fillRect(x, y, 1, 1, color);
    }
  }
  displayChangesQueued = true;
}

// Handle any websocket client connections/disconnections and messages.
// Any incoming message from websocket client is parsed into a pixel change and applied to the display, and is also broadcast to all clients to synchronize canvas state.
// Any new clients are added to a queue to be sent the current canvas state.
void webSocketEvent(uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", client);
      break;
    case WStype_CONNECTED:
    {
      IPAddress ip = ws.remoteIP(client);
      Serial.printf("Client:[%u] Connected from %d.%d.%d.%d url: %s\n", client, ip[0], ip[1], ip[2], ip[3], payload);
      clientsNeedingCanvas.push_back(client);
    }
    break;
    case WStype_TEXT:
    {
      ws.broadcastTXT(payload, length);

      JsonDocument msg;
      deserializeJson(msg, payload);

      bool clear = msg["clear"];
      if (clear) display.fillRect(0, 0, 128, 64, BLACK);
      else {
        bool pixelOn = msg["pixelOn"]; 
        int x = msg["x"]; 
        int y = msg["y"];
        int size = msg["size"];
        display.fillRect(x, y, size, size, pixelOn);
      }
      displayChangesQueued = true;
    }
    break;
    case WStype_BIN:
    {
      applyBinaryToDisplay(payload);
      int clients = ws.connectedClients();
      for (int i = 0; i < clients; i++) clientsNeedingCanvas.push_back(i);
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
    delay(300);                    
    digitalWrite(errorLed, HIGH);  // Turn off the red onboard LED by making the voltage HIGH
    delay(600);
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
  delay(500);
  display.clearDisplay();
  // Randomly select an image from the bootImages folder and apply it to the display after boot.
  randomSeed(analogRead(A0));
  std::string randomBootImage = "bootImages/icc" + std::to_string(random(1, 6)) + ".bin";
  file = LittleFS.open(randomBootImage.c_str(), "r");
  if (file) {
    char buf[(SCREEN_WIDTH*SCREEN_HEIGHT)/8];
    file.readBytes(buf, sizeof(buf));
    applyBinaryToDisplay((uint8_t*)buf);
  }
  displayChangesQueued = true;
}

// Convert the current canvas state into a binary representation with 1 bit per pixel and send it to the client.
void sendCanvasToClientsInQ() {
  int numBytes = (SCREEN_WIDTH * SCREEN_HEIGHT + 7) / 8;
  std::vector<uint8_t> binaryData(numBytes, 0);
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      int byteIndex = (y * SCREEN_WIDTH + x) / 8;
      int bitIndex = 7 - (y * SCREEN_WIDTH + x) % 8;
      binaryData[byteIndex] |= (display.getPixel(x,y) << bitIndex);
    }
  }
  while (!clientsNeedingCanvas.empty()) {
    int client = clientsNeedingCanvas.front();
    if (ws.clientIsConnected(client)) ws.sendBIN(client, binaryData.data(), binaryData.size());
    clientsNeedingCanvas.pop_front();
  }
}

void loop(void) {
  ElegantOTA.loop();

  if(millis() - lastWifiCheck > 2000) {
    if (WiFi.status() != WL_CONNECTED) digitalWrite(errorLed, LOW); // Turn on red onboard LED by setting voltage LOW            
    else digitalWrite(errorLed, HIGH); // Turn off red onboard LED by setting voltage HIGH
    lastWifiCheck = millis();
  }

  if (displayChangesQueued) {
    displayChangesQueued = false;
    display.display();
  }

  if (!clientsNeedingCanvas.empty()) {
    sendCanvasToClientsInQ();
  }
}