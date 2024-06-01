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
int bytesPerImage = (SCREEN_WIDTH*SCREEN_HEIGHT) / 8;

// Initialize WebSocket:
// REQUIRES WebSockets.h to have #define WEBSOCKETS_NETWORK_TYPE NETWORK_ESP8266_ASYNC OR loop() to have ws.loop();
WebSocketsServer ws = WebSocketsServer(81);
std::deque<int> clientsNeedingCanvas;

unsigned long lastWifiCheck = 0;

unsigned long lastImageSave = 0;
unsigned long currentlyDisplayedImage = 0;
bool newCanvasRequested = false;
bool nextImageRequested = false;
bool canvasDeletionRequested = false;

// Applies data from a binary file to the display.
void applyBinaryToDisplay(byte* buf) {
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

void handleIncomingCommand(byte* payload, size_t length) {
  ws.broadcastTXT(payload, length);

  JsonDocument msg;
  deserializeJson(msg, payload);

  if (msg["clear"]) display.fillRect(0, 0, 128, 64, BLACK);
  else if (msg["newCanvasRequested"]) newCanvasRequested = true;
  else if (msg["nextImageRequested"]) nextImageRequested = true;
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
// Any incoming message from websocket client is parsed into a pixel change and applied to the display, and is also broadcast to all clients to synchronize canvas state.
// Any new clients are added to a queue to be sent the current canvas state.
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
      handleIncomingCommand(payload, length);
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
  if (LittleFS.exists("/currentlyDisplayedImage")) {
    File file = LittleFS.open("/currentlyDisplayedImage", "r");
    if (file) {
      currentlyDisplayedImage = file.readStringUntil('\n').toInt();
      file.close();
    }
  }
  std::string filepath = "/icc" + std::to_string(currentlyDisplayedImage) + ".dat";
  file = LittleFS.open(filepath.c_str(), "r");
  byte buf[bytesPerImage];
  if (file) {
    file.readBytes((char*)buf, bytesPerImage);
    file.close();
  }
  applyBinaryToDisplay(buf);
}

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

// Send a binary representation of the canvas to all clients who need it
// Attempts to resend one time in case of transmission failure.
void sendCanvasToClientsInQ() {
  std::vector<byte> binaryData = convertCanvasToBinary();
  bool success = false;
  while (!clientsNeedingCanvas.empty()) {
    int client = clientsNeedingCanvas.front();
    if (ws.clientIsConnected(client)) success = ws.sendBIN(client, binaryData.data(), binaryData.size());
    if (!success && ws.clientIsConnected(client)) ws.sendBIN(client, binaryData.data(), binaryData.size());
    clientsNeedingCanvas.pop_front();
  }
}

// Saves the canvas state to the currently selected file.
void saveImage() {
  File file;
  std::string filepath = "/icc" + std::to_string(currentlyDisplayedImage) + ".dat";
  file = LittleFS.open(filepath.c_str(), "w+");
  if (file) {
    std::vector<byte> binaryData = convertCanvasToBinary();
    file.write(binaryData.data(), binaryData.size());
    file.close();
  }
}

// Displays the next saved image file found in memory.
void displayNextImageInRotation() {
  std::string filepath = "/icc" + std::to_string(++currentlyDisplayedImage) + ".dat";
  File file;
  byte buf[bytesPerImage];
  if (LittleFS.exists(filepath.c_str())) {
    file = LittleFS.open(filepath.c_str(), "r");
  }
  else {
    currentlyDisplayedImage = 0;
    filepath = "/icc" + std::to_string(currentlyDisplayedImage) + ".dat";
    file = LittleFS.open(filepath.c_str(), "r");
  }
  if (file) {
    file.readBytes((char*)buf, sizeof(buf));
    file.close();
  }
  applyBinaryToDisplay(buf);
  int clients = ws.connectedClients();
  for (int i = 0; i < clients; i++) clientsNeedingCanvas.push_back(i);
  file = LittleFS.open("/currentlyDisplayedImage", "w+");
  if (file) {
    file.printf("%lu", currentlyDisplayedImage);
    file.close();
  }
}

// Creates a new blank file on the server and sets it as the canvas. 
void createBlankImage() {
  File file;
  unsigned long imageNumber = 1;
  std::string filepath = "/icc" + std::to_string(imageNumber) + ".dat";
  while (LittleFS.exists(filepath.c_str())) {
    filepath = "/icc" + std::to_string(++imageNumber) + ".dat";
  }
  file = LittleFS.open(filepath.c_str(), "w+");
  byte buf[bytesPerImage] = {0};
  if (file) {
    file.write(buf, sizeof(buf));
    file.close();
  }
  currentlyDisplayedImage = imageNumber - 1;
  displayNextImageInRotation();
}

void deleteCurrentCanvas() {

}

void loop(void) {
  ElegantOTA.loop();

  if(millis() - lastWifiCheck > 2000) {
    if (WiFi.status() != WL_CONNECTED) digitalWrite(errorLed, LOW); // Turn on red onboard LED by setting voltage LOW            
    else digitalWrite(errorLed, HIGH); // Turn off red onboard LED by setting voltage HIGH
    lastWifiCheck = millis();
  }
  
  if (millis() - lastImageSave > 1000) { 
    saveImage();
    lastImageSave = millis();
  }

  if (canvasDeletionRequested) {
    deleteCurrentCanvas();
    canvasDeletionRequested = false;
  }

  if (newCanvasRequested) {
    createBlankImage();
    newCanvasRequested = false;
  }

  if (nextImageRequested) {
    displayNextImageInRotation();
    nextImageRequested = false;
  }

  if (displayChangesQueued) {
    displayChangesQueued = false;
    display.display();
  }

  if (!clientsNeedingCanvas.empty()) {
    sendCanvasToClientsInQ();
  }
}