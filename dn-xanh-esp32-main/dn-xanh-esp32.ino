#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Arduino_JSON.h>
#include <HTTPClient.h>

// Define pins
#define DOOR_ECHO_PIN 23
#define SOUND_TRIGGER_PIN 5
#define SOUND_ECHO_PIN 18

// Constants
const String EMBEDDED_SYSTEM_ID = "DN-SMT-001_RECYCLABLE"; // Change it
const String WASTE_TYPE = "RECYCLABLE"; // Change it
enum State {IDLE, WAITING_FOR_OPEN_DOOR, OPENING_DOOR, COLLECTING_DATA, SERVER_PROCESSING, CLAIM_REWARD, REQUESTING_FINISH};
const double SOUND_SPEED = 0.034;
const unsigned long TIMEOUT = 30000;
const String SERVER_BASE_URL = "http://api.danangxanh.top/api";
const String CAMERA_BASE_URL = "http://192.168.137.21";

// Wifi config
const char* SSID = "minh_nguyenanh";
const char* PASSWORD = "123456789";

// Static IP config
IPAddress localIP(192, 168, 137, 20);
IPAddress gateway(192, 168, 137, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// Global variables
State state = IDLE;
bool isConnectedToWifi = false;
uint32_t currentClientId = 0;

String stateToString(State state) {
  switch (state) {
    case IDLE:
      return "IDLE";

    case OPENING_DOOR:
      return "OPENING_DOOR";

    case COLLECTING_DATA:
      return "COLLECTING_DATA";

    case SERVER_PROCESSING:
      return "SERVER_PROCESSING";

    case CLAIM_REWARD:
      return "CLAIM_REWARD";
  }
}

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Connected to AP successfully!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("WiFi connected");
  isConnectedToWifi = true;

  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Disconnected from WiFi access point");
  Serial.print("WiFi lost connection. Reason: ");
  Serial.println(info.wifi_sta_disconnected.reason);
  Serial.println("Trying to Reconnect");
  WiFi.begin(SSID, PASSWORD);
}

// Initialize WiFi
void initWiFi() {
  WiFi.disconnect(true);

  delay(200);

  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // // Dynamic IP
  // WiFi.mode(WIFI_STA);

  // Static IP
  if (!WiFi.config(localIP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }

  WiFi.begin(SSID, PASSWORD);
  Serial.println("Connecting to WiFi ...");
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    Serial.print("Received message");
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      currentClientId = client->id();
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void initDoor() {
  pinMode(DOOR_ECHO_PIN, INPUT);
}

void initSound() {
  pinMode(SOUND_TRIGGER_PIN, OUTPUT);
  pinMode(SOUND_ECHO_PIN, INPUT);
}

void setup() {
  Serial.begin(115200);
  initWiFi();
  initWebSocket();
  initDoor();
  initSound();

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "DN Xanh Embedded System: " + EMBEDDED_SYSTEM_ID);
  });

  // Start server
  server.begin();
}

void sendMessage(const String message) {
  ws.text(currentClientId, message);
  Serial.println(message);
}

void sendError(const String error) {
  JSONVar responseData;
  responseData["type"] = "ERROR";
  responseData["message"] = error;
  String jsonresponseData = JSON.stringify(responseData);
  sendMessage(jsonresponseData);
}

bool requestGet(const String path, JSONVar& responseObj) {
  bool result = true;

  // Verify wifi connection
  if (isConnectedToWifi) {
    HTTPClient http;

    // configure traged server and url
    String url = SERVER_BASE_URL + path;
    http.begin(url.c_str());  //HTTP

    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      if (httpCode == 200) {
        String responseJson = http.getString();
        responseObj = JSON.parse(responseJson);

        if (JSON.typeof(responseObj) == "undefined") {
          Serial.println("[HTTP] GET: Parsing response failed!");
          sendError("Có lỗi xảy ra");
          result = false;
        }
      } else {
        Serial.printf("[HTTP] GET: failed, error: %s\n", http.errorToString(httpCode).c_str());
        sendError("Có lỗi xảy ra");
        result = false;
      }
    } else {
      Serial.printf("[HTTP] GET: failed, error: %s\n", http.errorToString(httpCode).c_str());
      sendError("Không thể gửi dữ liệu đến server trung tâm");
      result = false;
    }

    http.end();

    return httpCode;
  } else {
    result = false;
  }

  if (!result) setState(IDLE);

  return result;
}

bool requestPost(const String path, const String bodyJson, JSONVar& responseObj) {
  bool result = true;

  // Verify wifi connection
  if (isConnectedToWifi) {
    HTTPClient http;

    // configure traged server and url
    String url = SERVER_BASE_URL + path;
    http.begin(url.c_str());  //HTTP

    // Add headers
    http.addHeader("Content-Type", "application/json");

    // start connection and send HTTP header
    Serial.println(bodyJson);
    int httpCode = http.POST(bodyJson);


    // httpCode will be negative on error
    if (httpCode > 0) {
      // // HTTP header has been send and Server response header has been handled
      // Serial.printf("[HTTP] POST... code: %d\n", httpCode);

      if (httpCode == 201) {
        String responseJson = http.getString();
        responseObj = JSON.parse(responseJson);

        if (JSON.typeof(responseObj) == "undefined") {
          Serial.println("[HTTP] POST: Parsing response failed!");
          sendError("Có lỗi xảy ra");
          result = false;
        }
      } else {
        Serial.printf("[HTTP] POST: failed, error: %s\n", http.errorToString(httpCode).c_str());
        sendError("Có lỗi xảy ra");
        result = false;
      }
    } else {
      Serial.printf("[HTTP] POST: failed, error: %s\n", http.errorToString(httpCode).c_str());
      sendError("Không thể gửi dữ liệu đến server trung tâm");
      result = false;
    }

    http.end();
  } else {
    result = false;
  }

  if (!result) setState(IDLE);

  return result;
}

void setState(State newState) {
  state = newState;

  JSONVar responseData;
  responseData["type"] = "SET_STATE";
  responseData["state"] = stateToString(newState);
  String jsonresponseData = JSON.stringify(responseData);
  sendMessage(jsonresponseData);
}

bool checkDoor() {
  const bool isOpen = digitalRead(DOOR_ECHO_PIN);
  return isOpen;
}

double getHeight() {
  // Clears the trigPin
  digitalWrite(SOUND_TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(SOUND_TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(SOUND_TRIGGER_PIN, LOW);
  
  // Reads the echoPin, returns the sound wave travel time in microseconds
  unsigned long soundDuration = pulseIn(SOUND_ECHO_PIN, HIGH);
  
  // Calculate the distance
  double distanceCm = soundDuration * SOUND_SPEED/2;
  
  // Prints the distance in the Serial Monitor
  // Serial.print("Distance (cm): ");
  // Serial.println(distanceCm);

  return distanceCm;
}

void breakLoop() {
  // Limiting the number of web socket clients
  ws.cleanupClients();
  delay(1000);
}

void loop() {
  static unsigned long beginTime = 0;
  static double previousHeight = 0;
  static String smartRecycleBinClassificationHistoryId = "";

  // Verify wifi connection
  if (!isConnectedToWifi) return breakLoop();

  // Verify websocket connection
  if (currentClientId == 0) {
    Serial.println("Waiting for client connect...");
    state = IDLE;

    return breakLoop();
  } 
  
  bool isDoorOpened = checkDoor();
  double height = getHeight();

  // Send sensors data to monitor
  JSONVar sensorData;
  sensorData["type"] = "SENSORS_DATA";
  sensorData["isDoorOpened"] = String(isDoorOpened);
  sensorData["height"] = String(height);
  String jsonSensorData = JSON.stringify(sensorData);
  sendMessage(jsonSensorData);

  // Check door
  if (isDoorOpened && state != OPENING_DOOR) {
    setState(OPENING_DOOR);
    beginTime = millis();

    return breakLoop();
  }

  if (state == IDLE) {
    previousHeight = height;
  }

  if (state == OPENING_DOOR && !isDoorOpened) {
    setState(COLLECTING_DATA);
    beginTime = millis();

    return breakLoop();
  }

  if (state == COLLECTING_DATA) {
    double sumHeight = 0;
    String wasteTypePredictions[5];
    JSONVar wasteTypePredictionsCount;
    for (int i = 0; i < 5; ++i) {
      // Capture & classify image
      JSONVar captureAndClassifyResponseData;
      if (!requestGet("/smart-recycle-bin/capture-and-classify", captureAndClassifyResponseData)) return breakLoop();
      String wasteTypePrediction = captureAndClassifyResponseData["wasteTypePrediction"];

      wasteTypePredictions[i] = wasteTypePrediction;
      if (JSON.typeof(wasteTypePredictionsCount[wasteTypePrediction]) == "undefined") wasteTypePredictionsCount[wasteTypePrediction] = 1;
      else wasteTypePredictionsCount[wasteTypePrediction] = ((int) wasteTypePredictionsCount[wasteTypePrediction]) + 1;

      // Calculate height
      height = getHeight();
      sumHeight += height;
      delay(200);
    }
    double avgHeight = sumHeight / 5;

    // Get highest wasteType prediction
    String highestWasteTypePrediction;
    for (int i = 0; i < 3; ++i) {
      String wasteTypePrediction = wasteTypePredictions[i];
      if ((int) wasteTypePredictionsCount[wasteTypePrediction] > (int) wasteTypePredictionsCount[highestWasteTypePrediction])
        highestWasteTypePrediction = wasteTypePrediction;
    }

    // Send to server
    setState(SERVER_PROCESSING);

    JSONVar classifyRequestData;
    classifyRequestData["volume"] = avgHeight;
    classifyRequestData["embeddedSystemId"] = EMBEDDED_SYSTEM_ID;
    classifyRequestData["wasteType"] = highestWasteTypePrediction;

    JSONVar classifyResponseData;
    if (!requestPost("/smart-recycle-bin/classify", JSON.stringify(classifyRequestData), classifyResponseData)) return breakLoop();

    smartRecycleBinClassificationHistoryId = String(classifyResponseData["smartRecycleBinClassificationHistoryId"]);
    bool isCorrect = (bool) classifyResponseData["isCorrect"];
    String token = classifyResponseData["token"];

    if (!isCorrect) {
      sendError("Phân loại rác chưa đúng");
      setState(IDLE);
    }

    JSONVar responseData;
    responseData["type"] = "BUILD_QR";
    responseData["isCorrect"] = isCorrect;
    responseData["token"] = token;
    String jsonresponseData = JSON.stringify(responseData);
    sendMessage(jsonresponseData);

    setState(CLAIM_REWARD);
  }

  if (state == CLAIM_REWARD) {
    JSONVar checkClaimRequestData;
    checkClaimRequestData["smartRecycleBinClassificationHistoryId"] = smartRecycleBinClassificationHistoryId;

    JSONVar checkClaimResponseData;
    bool isRequestedSuccess = requestPost("/smart-recycle-bin/check-claim-reward", JSON.stringify(checkClaimRequestData), checkClaimResponseData);
    if (isRequestedSuccess) {
      bool isClaimed = checkClaimResponseData["isClaimed"];
      if (isClaimed) {
        sendMessage("Cảm ơn đã sử dụng thùng rác thông minh!");
        setState(IDLE);
      }
    }
  }

  if (state != IDLE) {
    // Check timeout
    unsigned long processingTimeConsumed = millis() - beginTime;
    if (processingTimeConsumed > TIMEOUT) {
      sendError("Đã quá thời gian chờ");
      setState(IDLE);

      return breakLoop();
    } 
  }

  return breakLoop();
}