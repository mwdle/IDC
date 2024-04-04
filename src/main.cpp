#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <AsyncTCP.h>
#endif

#include <LittleFS.h>

#define SPIFFS LittleFS

#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <queue>

#define errorLed D0

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

using std::smatch;
using std::regex;
using std::regex_match;
using std::string;
using std::stringstream;

// Only put your credentials here if you aren't storing them on the filesystem. You can learn more about this in the README.
String ssid = "";
String password = "";

IPAddress subnet(255, 255, 255, 0);
IPAddress gateway(192, 168, 0, 1);
IPAddress ip(192, 168, 0, 98);

AsyncWebServer server(80);

std::queue<string> queue;

// Initialize display:
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// stringstream pixels;

// Initialize WebSocket:
AsyncWebSocket ws("/ws");

unsigned long lastWifiCheck = 0;

volatile bool displayChangesQueued = false;

// Handle any incoming display data from the web interface, and mirror it to the display.
void recvWebSktMsg(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    string msg;
    for(size_t i=0; i < info->len; i++) {
      msg += (char)data[i];
    }
    queue.push(msg);
  }
}

// Handle WebSocket Events
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      // newClientConnected();
      // // ws.text(client->id(), pixels.str().c_str());
      // notifyClients(pixels.str().c_str());
      // ESP.wdtFeed();
      // pixels.clear();
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
        recvWebSktMsg(arg, data, len);
        break;
    case WS_EVT_PONG:
      break;
    case WS_EVT_ERROR:
     break;
  }
}

// Initialize FS
void initLittleFS() {
  if (!LittleFS.begin()) {
    Serial.println("An error has occurred while mounting FS");
  }
  else{
    Serial.println("FS mounted successfully");
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void setup(void) {
  // Initialize LED Pina
  pinMode(errorLed, OUTPUT);

  Serial.begin(115200);

  initLittleFS();
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
  Serial.println("Wifi Connected! IP Address: " + WiFi.localIP().toString());

  ElegantOTA.begin(&server);

  // Start OTA server
  server.begin();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.serveStatic("/", LittleFS, "/");

  initWebSocket();

  // Start web canvas and websocket server.
  server.begin();

  // Initialize display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println("SSD1306 allocation failed");
  }
  delay(2000);
  display.clearDisplay();
  display.display();
}

void loop(void) {
  ElegantOTA.loop();

  if(millis() - lastWifiCheck > 3000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) digitalWrite(errorLed, LOW); // Turn on red onboard LED by setting voltage LOW            
    else digitalWrite(errorLed, HIGH); // Turn off red onboard LED by setting voltage HIGH
  } 

  while (!queue.empty()) {
    stringstream msg = stringstream(queue.front());
    ws.textAll(msg.str().c_str());
    queue.pop();
    string values[7];
    int index = 0;
    for (string chunk; getline(msg, chunk, ';');) {
      values[index++] = chunk;
      ESP.wdtFeed();
    }
    if (values[0] == "1") display.fillRect(0, 0, 128, 64, BLACK);
    else display.fillRect(stoi(values[3]), stoi(values[4]), stoi(values[6]), stoi(values[6]), stoi(values[2]));
    display.display();
    ESP.wdtFeed();
    delay(20);
  }

  if (displayChangesQueued) {
    displayChangesQueued = false;
    display.display();
  }

  ws.cleanupClients();
}