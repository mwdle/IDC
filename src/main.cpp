#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
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

std::deque<std::string> queue();

std::deque<AsyncWebSocketClient*> newClientQueue;
// string pixel;

// Initialize display:
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Initialize WebSocket:
AsyncWebSocket ws("/ws");

unsigned long lastWifiCheck = 0;

// Handle any incoming display data from the web interface, and mirror it to the display.
void recvWebSktMsg(void *arg, uint8_t *data, size_t len) {
  ESP.wdtFeed();
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    
    data[len] = 0;
    std::string msg((char*)data);
    queue.push_back(msg);
  }
}

// Handle WebSocket Events
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
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
    Serial.println("\nAn error has occurred while mounting FS");
  }
  else{
    Serial.println("\nFS mounted successfully");
  }
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
  Serial.printf("Wifi Connected! IP Address: %s\n", WiFi.localIP().toString().c_str());

  ElegantOTA.begin(&server);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.serveStatic("/", LittleFS, "/");
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.begin();

  // Initialize display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println("Display intialization failed");
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

  if (!queue.empty() && ws.availableForWriteAll()) {
    Serial.printf("QS: %d\n", queue.size());
    Serial.printf("Remaining Heap: %u\n", ESP.getFreeHeap());
    std::string msg = queue.front();
    ws.textAll(msg.c_str());
    ESP.wdtFeed();
    int values[7];
    int index = 0;
    std::string::const_iterator start = msg.begin();
    std::string::const_iterator end = msg.end();
    std::string::const_iterator next = std::find(start, end, ';');
    while (next != end) {
        values[index++] = std::stoi(std::string(start, next));
        start = next + 1;
        next = std::find(start, end, ';');
        ESP.wdtFeed();
    }
    values[index++] = std::stoi(std::string(start, next));

    if (values[0] == 1) display.fillRect(0, 0, 128, 64, BLACK);
    else display.fillRect(values[3], values[4], values[6], values[6], values[2]);
    display.display();
    ESP.wdtFeed();
    queue.pop_front();
  }

  ws.cleanupClients(4);
}