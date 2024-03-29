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
#include <WebSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define errorLed D0

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

using std::smatch;
using std::regex;
using std::regex_match;
using std::string;

// Only put your credentials here if you aren't storing them on the filesystem. You can learn more about this in the README.
String ssid = "";
String password = "";

IPAddress subnet(255, 255, 255, 0);
IPAddress gateway(192, 168, 0, 1);
IPAddress ip(192, 168, 0, 98);

AsyncWebServer server(80);

// Initialize display:
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

std::stringstream pixels;

// Initialize WebSocket:
AsyncWebSocket ws("/ws");

unsigned long lastWifiCheck = 0;

volatile bool displayChangesQueued = false;

// Handles incoming commands from WebSerial.
// Supports /setPixel x y \<status\> (where \<status\> should be replaced with either "on" or "off") for toggling I2C display pixels.
// Supports "/restart" for restarting the ESP device.
// Littered with ESP.wdtFeed() (resets the software watchdog timer), preventing unexpected resets during delays handling many messages in succession.
void recvMsg(uint8_t *data, size_t len) {
  string msg;
  msg.reserve(len);
  for(size_t i=0; i < len; i++){
    msg += char(data[i]);
    ESP.wdtFeed();
  }

  regex setPixelCmd(R"(^/setPixel (\d{1,3}) (\d{1,3}) (\w{2,3})$)");
  regex restartCmd(R"(/restart)");

  smatch match;

  if (regex_match(msg, match, setPixelCmd)) {
    ESP.wdtFeed();
    int x = stoi(match[1]);
    int y = stoi(match[2]);
    string color = match[3].str();
    if (color == "on") display.drawPixel(x, y, WHITE);
    else if (color == "off") display.drawPixel(x, y, BLACK);
    ESP.wdtFeed();
    displayChangesQueued = true;
    ESP.wdtFeed();
    WebSerial.printf("Set Pixel command received. The pixel: (%d, %d) has been set to %s\n", x, y, color.c_str());
  }
  else if (regex_match(msg, match, restartCmd)) {
    WebSerial.println("Restart command received. Restarting . . . \n");
    ESP.restart();
  } 
  else { WebSerial.print("New message: "); WebSerial.println(msg.c_str()); }
  ESP.wdtFeed();
}

// Setup WebSocket Callbacks:
void notifyClients(String state) {
  ws.textAll(state);
}

// Handle any incoming display data from the web interface, and mirror it to the display.
void recvWebSktMsg(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    std::stringstream message((char*)data);
    string messages[5];
    int index = 0;
    for (string chunk; getline(message, chunk, ';');) {
      messages[index++] = chunk;
    }
    int x = stoi(messages[0]);
    int y = stoi(messages[1]);
    int brushSize = stoi(messages[2]);
    uint16_t color = stoi(messages[3]);
    bool clearCanvas = stoi(messages[4]);
    if (clearCanvas) { 
      display.fillRect(0, 0, 128, 64, BLACK); 
      notifyClients("CC");  
    }
    else { 
      display.fillRect(x, y, brushSize, brushSize, color); 
      std::stringstream message;
      message << x << ";" << y << ";" << brushSize << ";" << color << ";";
      notifyClients(message.str().c_str());
    }

    displayChangesQueued = true;
  }
}

// Handle WebSocket Events
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      WebSerial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      for (int i = 0; i < SCREEN_HEIGHT; i++) {
        for (int j = 0; j < SCREEN_WIDTH; j++) {
          pixels << display.getPixel(j, i);
        }
        ESP.wdtFeed();
      }
      ws.text(client->id(), pixels.str().c_str());
      pixels.clear();
      break;
    case WS_EVT_DISCONNECT:
      WebSerial.printf("WebSocket client #%u disconnected\n", client->id());
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

  WebSerial.begin(&server);
  WebSerial.msgCallback(recvMsg);

  // Start OTA/Serial server
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
    WebSerial.println("SSD1306 allocation failed");
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

  if (displayChangesQueued) {
    displayChangesQueued = false;
    display.display();
  }

  ws.cleanupClients();
}