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

int bytesPerImage = (SCREEN_WIDTH*SCREEN_HEIGHT) / 8;

// Only put your Wi-Fi credentials here if you aren't storing them on the filesystem. You can learn more about this in the README.
String ssid = "";
String password = "";
IPAddress subnet(255, 255, 255, 0);
IPAddress gateway(192, 168, 0, 1);
// Static IP 192.168.0.98 (change if needed)
IPAddress ip(192, 168, 0, 98);
// Variable to keep track of the last time the Wi-Fi connection status was checked.
unsigned long lastWifiCheck = 0;

AsyncWebServer server(80);

// Initialize display with ESP8266 default SCL and SDA pins -> D1 and D2, respectively.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool displayChangesQueued = false;

// Initialize WebSocket:
// REQUIRES WebSockets.h to have #define WEBSOCKETS_NETWORK_TYPE NETWORK_ESP8266_ASYNC OR loop() to have ws.loop();
WebSocketsServer ws = WebSocketsServer(81);
std::deque<int> clientsNeedingCanvas;

// Variables for canvas operations.
unsigned long lastCanvasSave = 0;
unsigned long currentCanvas = 0;
bool newCanvasRequested = false;
bool nextCanvasRequested = false;
bool deleteCanvasRequested = false;

// Applies a binary representation of an image to the physical display.
void applyImageToCanvas(byte* buf) {
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

// Handles a websocket client message containing a command.
void handleCommand(byte* payload) {
  JsonDocument msg;
  deserializeJson(msg, payload);
  if (msg["clear"]) display.fillRect(0, 0, 128, 64, BLACK);
  else if (msg["newCanvasRequested"]) newCanvasRequested = true;
  else if (msg["nextCanvasRequested"]) nextCanvasRequested = true;
  else if (msg["deleteCanvasRequested"]) deleteCanvasRequested = true;
  else {
    bool pixelOn = msg["pixelOn"]; 
    int x = msg["x"]; 
    int y = msg["y"];
    int size = msg["size"];
    display.fillRect(x, y, size, size, pixelOn);
  }
  displayChangesQueued = true;
}

// Handle any websocket client connections/disconnections and messages.
// Any incoming command is relayed to all connected clients and handled accordingly.
// Any incoming binary is relayed to all connected clients and applied to the physical display.
// Any newly connected clients are added to the "clientsNeedingCanvas" queue.
void handleWebSocketEvent(byte client, WStype_t type, byte* payload, size_t length) {
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
      handleCommand(payload);
    }
    break;
    case WStype_BIN:
    {
      applyImageToCanvas(payload);
      int clients = ws.connectedClients();
      for (int i = 0; i < clients; i++) clientsNeedingCanvas.push_back(i);
    }
    break;
  }
}

// Returns a binary representation of the current canvas state.
std::vector<byte> convertCanvasToBinary() {
  std::vector<byte> binaryData(bytesPerImage, 0);
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      int byteIndex = (y * SCREEN_WIDTH + x) / 8;
      int bitIndex = 7 - (y * SCREEN_WIDTH + x) % 8;
      binaryData[byteIndex] |= (display.getPixel(x,y) << bitIndex);
    }
  }
  return binaryData;
}

// Send a binary representation of the canvas to all clients in the "clientsNeedingCanvas" queue.
// Attempts to resend one time in case of transmission failure.
void sendCanvasToClientsInQueue() {
  std::vector<byte> binaryData = convertCanvasToBinary();
  bool success = false;
  while (!clientsNeedingCanvas.empty()) {
    int client = clientsNeedingCanvas.front();
    if (ws.clientIsConnected(client)) success = ws.sendBIN(client, binaryData.data(), binaryData.size());
    if (!success && ws.clientIsConnected(client)) ws.sendBIN(client, binaryData.data(), binaryData.size());
    clientsNeedingCanvas.pop_front();
  }
}

// Saves the canvas state to the currently selected canvas file.
void saveCanvasToFile() {
  File file;
  const char* filepath = ("/" + std::to_string(currentCanvas) + ".dat").c_str();
  file = LittleFS.open(filepath, "w+");
  if (file) {
    std::vector<byte> binaryData = convertCanvasToBinary();
    file.write(binaryData.data(), binaryData.size());
    file.close();
  }
}

// Loads the next stored canvas from memory if found, otherwise loads the first canvas.
// Adds all connected clients to the "clientsNeedingCanvas" queue.
void switchToNextCanvas() {
  std::string filepath = ("/" + std::to_string(++currentCanvas) + ".dat");
  File file;
  if (LittleFS.exists(filepath.c_str())) {
    file = LittleFS.open(filepath.c_str(), "r");
  }
  else {
    currentCanvas = 0;
    filepath = ("/" + std::to_string(currentCanvas) + ".dat");
    file = LittleFS.open(filepath.c_str(), "r");
  }
  byte buf[bytesPerImage];
  if (file) {
    file.readBytes((char*)buf, sizeof(buf));
    file.close();
  }
  applyImageToCanvas(buf);
  int clients = ws.connectedClients();
  for (int i = 0; i < clients; i++) clientsNeedingCanvas.push_back(i);
  file = LittleFS.open("/currentCanvas", "w+");
  if (file) {
    file.printf("%lu", currentCanvas);
    file.close();
  }
}

// Creates a new blank canvas file in memory and switches to it.
void createNewCanvas() {
  File file;
  unsigned long imageNumber = 0;
  std::string filepath = ("/" + std::to_string(imageNumber) + ".dat");
  while (LittleFS.exists(filepath.c_str())) {
    filepath = ("/" + std::to_string(++imageNumber) + ".dat");
  }
  file = LittleFS.open(filepath.c_str(), "w+");
  byte buf[bytesPerImage];
  memset(buf, 0, bytesPerImage);
  if (file) {
    file.write(buf, sizeof(buf));
    file.close();
  }
  currentCanvas = imageNumber - 1;
  switchToNextCanvas();
}

// Deletes the currently selected canvas from memory and switch to the next available canvas.
// Replaces the deleted canvas with the very last canvas to maintain continuity.
void deleteCurrentCanvas() {
  unsigned long replacementCanvas = currentCanvas + 1;
  std::string replacementPath = ("/" + std::to_string(replacementCanvas) + ".dat");
  while (LittleFS.exists(replacementPath.c_str())) {
    replacementPath = ("/" + std::to_string(++replacementCanvas) + ".dat");
  }
  replacementPath = ("/" + std::to_string(--replacementCanvas) + ".dat");
  std::string currentCanvasPath = ("/" + std::to_string(currentCanvas) + ".dat");
  if (LittleFS.remove(currentCanvasPath.c_str())) {
    if (replacementCanvas != currentCanvas) {
      LittleFS.rename(replacementPath.c_str(), currentCanvasPath.c_str());
      currentCanvas--;
      switchToNextCanvas();
    }
    else if (currentCanvas == 0) createNewCanvas();
    else switchToNextCanvas();
  }
}

void setup(void) {
  // Initialize LED Pina
  pinMode(errorLed, OUTPUT);

  Serial.begin(115200);

  // Initialize Filesystem and read Wi-fi credentials from file.
  while (!LittleFS.begin()) {
    Serial.println("\nAn error has occurred while mounting FS");
    digitalWrite(errorLed, LOW);   // Turn on the red onboard LED by making the voltage LOW
    delay(200);                    
    digitalWrite(errorLed, HIGH);  // Turn off the red onboard LED by making the voltage HIGH
    delay(300);
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
  WiFi.hostname("esp8266");
  WiFi.begin(ssid.c_str(), password.c_str());
  while(WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(errorLed, LOW);   // Turn on the red onboard LED by making the voltage LOW
    delay(300);                    
    digitalWrite(errorLed, HIGH);  // Turn off the red onboard LED by making the voltage HIGH
    delay(600);
  }
  Serial.printf("Wifi connection succcessful! IP Address: %s\n", WiFi.localIP().toString().c_str());

  // Initialize OTA & Websocket servers.
  ElegantOTA.begin(&server);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.serveStatic("/", LittleFS, "/");
  ws.begin();
  ws.onEvent(handleWebSocketEvent);
  server.begin();

  // Initialize display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println("Display intialization failed");
  }
  delay(500);
  display.clearDisplay();
  if (LittleFS.exists("/currentCanvas")) {
    File file = LittleFS.open("/currentCanvas", "r");
    if (file) {
      currentCanvas = file.readStringUntil('\n').toInt();
      file.close();
    }
  }
  const char* filepath = ("/" + std::to_string(currentCanvas) + ".dat").c_str();
  if (LittleFS.exists(filepath)) {
    byte buf[bytesPerImage];
    file = LittleFS.open(filepath, "r");
    if (file) {
      file.readBytes((char*)buf, bytesPerImage);
      file.close();
    }
    applyImageToCanvas(buf);
  }
  else createNewCanvas();
}

void loop(void) {
  ElegantOTA.loop();

  if(millis() - lastWifiCheck > 2000) {
    if (WiFi.status() != WL_CONNECTED) digitalWrite(errorLed, LOW); // Turn on red onboard LED by setting voltage LOW            
    else digitalWrite(errorLed, HIGH); // Turn off red onboard LED by setting voltage HIGH
    lastWifiCheck = millis();
  }
  
  if (millis() - lastCanvasSave > 1000) { 
    saveCanvasToFile();
    lastCanvasSave = millis();
  }

  if (deleteCanvasRequested) {
    deleteCurrentCanvas();
    deleteCanvasRequested = false;
  }

  if (newCanvasRequested) {
    createNewCanvas();
    newCanvasRequested = false;
  }

  if (nextCanvasRequested) {
    switchToNextCanvas();
    nextCanvasRequested = false;
  }

  if (displayChangesQueued) {
    displayChangesQueued = false;
    display.display();
  }

  if (!clientsNeedingCanvas.empty()) {
    sendCanvasToClientsInQueue();
  }
}