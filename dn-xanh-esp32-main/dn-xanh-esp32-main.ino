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
#define LED_PIN 17

// Constants
const String EMBEDDED_SYSTEM_ID = "DN-SMT-001_RECYCLABLE";  // Change it
const String WASTE_TYPE = "RECYCLABLE";                     // Change it
enum State { IDLE,
             WAITING_FOR_OPEN_DOOR,
             OPENING_DOOR,
             COLLECTING_DATA,
             CLAIM_REWARD,
             REQUESTING_FINISH };
const double SOUND_SPEED = 0.034;
const unsigned long TIMEOUT = 30000;
const String SERVER_BASE_URL = "http://api-dnx.passgenix.com/api";
const String FRONT_ESP32_URL = "http://192.168.100.100";

// Wifi config
const char *SSID = "MERCUSYS_DCE1";
const char *PASSWORD = "123456789";


// Static IP config
IPAddress localIP(192, 168, 100, 30);  // Change it
IPAddress gateway(192, 168, 100, 1);
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

    case WAITING_FOR_OPEN_DOOR:
      return "WAITING_FOR_OPEN_DOOR";

    case OPENING_DOOR:
      return "OPENING_DOOR";

    case COLLECTING_DATA:
      return "COLLECTING_DATA";

    case CLAIM_REWARD:
      return "CLAIM_REWARD";

    case REQUESTING_FINISH:
      return "REQUESTING_FINISH";
  }
}

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("Connected to AP successfully!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("WiFi connected");
  isConnectedToWifi = true;

  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
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
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    Serial.print("Received message");
  }
}

void onWebsocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      currentClientId = client->id();
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      currentClientId = 0;
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
  ws.onEvent(onWebsocketEvent);
  server.addHandler(&ws);
}

void initDoor() {
  pinMode(DOOR_ECHO_PIN, INPUT);
}

void initSound() {
  pinMode(SOUND_TRIGGER_PIN, OUTPUT);
  pinMode(SOUND_ECHO_PIN, INPUT);
}

void initLed() {
  pinMode(LED_PIN, OUTPUT);
}

void setup() {
  Serial.begin(115200);
  initWiFi();
  initWebSocket();
  initDoor();
  initSound();
  initLed();

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "DN Xanh Embedded System: " + EMBEDDED_SYSTEM_ID);
  });

  server.on("/request-open-door", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(201, "text/plain", "{\"message\":\"OK\"}");
    setState(WAITING_FOR_OPEN_DOOR);
  });

  // Start server
  server.begin();
}

void sendMessage(const String message) {
  if (currentClientId == 0) {
    Serial.println("Skip send message due to waiting for connection...");
    return;
  }

  ws.text(currentClientId, message);
  Serial.println(message);
}

// void sendError(const String error) {
//   JSONVar responseData;
//   responseData["type"] = "ERROR";
//   responseData["message"] = error;
//   String jsonResponseData = JSON.stringify(responseData);
//   sendMessage(jsonResponseData);
// }
void sendError(const String& error, const String& errorCode = "GENERIC_ERROR") {
  JSONVar responseData;
  responseData["type"] = "ERROR";
  responseData["code"] = errorCode;
  responseData["message"] = error;
  String jsonResponseData = JSON.stringify(responseData);
  sendMessage(jsonResponseData);
}


bool requestGet(const String serverBaseUrl, const String path, JSONVar &responseObj) {
  bool result = true;

  // Verify wifi connection
  if (isConnectedToWifi) {
    HTTPClient http;

    // configure traged server and url
    String url = serverBaseUrl + path;
    Serial.printf("[HTTP] GET: %s\n", url.c_str());

    http.begin(url.c_str());  //HTTP

    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      if (httpCode == 200) {
        String responseJson = http.getString();
        responseObj = JSON.parse(responseJson);

        if (JSON.typeof(responseObj) == "undefined") {
          Serial.println("[HTTP-ERROR] GET: Parsing response failed!");
          sendError("Có lỗi xảy ra");
          result = false;
        }
      } else {
        Serial.printf("[HTTP-ERROR] GET: Received, error: %d\n", httpCode);
        sendError("Có lỗi xảy ra");
        result = false;
      }
    } else {
      Serial.printf("[HTTP-ERROR] GET: Can't send request: %d\n", httpCode);
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

bool requestPost(const String serverBaseUrl, const String path, const String bodyJson, JSONVar &responseObj) {
  bool result = true;

  // Verify wifi connection
  if (isConnectedToWifi) {
    HTTPClient http;

    // configure traged server and url
    String url = serverBaseUrl + path;
    Serial.printf("[HTTP] POST: %s\n", url.c_str());

    http.begin(url.c_str());  //HTTP

    // Add headers
    http.addHeader("Content-Type", "application/json");

    // start connection and send HTTP header
    Serial.println(bodyJson);
    int httpCode = http.POST(bodyJson);


    // httpCode will be negative on error
    if (httpCode > 0) {
      if (httpCode == 201) {
        String responseJson = http.getString();
        responseObj = JSON.parse(responseJson);

        if (JSON.typeof(responseObj) == "undefined") {
          Serial.println("[HTTP-ERROR] POST: Parsing response failed!");
          sendError("Có lỗi xảy ra");
          result = false;
        }
      } else {
        Serial.printf("[HTTP-ERROR] POST: Received: %d\n", httpCode);
        sendError("Có lỗi xảy ra");
        result = false;
      }
    } else {
      Serial.printf("[HTTP-ERROR] POST: Can't send request: %d\n", httpCode);
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
  String jsonResponseData = JSON.stringify(responseData);
  sendMessage(jsonResponseData);
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
  double distanceCm = soundDuration * SOUND_SPEED / 2;

  // Prints the distance in the Serial Monitor
  // Serial.print("Distance (cm): ");
  // Serial.println(distanceCm);

  return distanceCm;
}

void setLed(bool isOn) {
  if (isOn) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }
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

  // Get sensors data
  bool isDoorOpened = checkDoor();
  double height = getHeight();

  // Send sensors data to monitor
  JSONVar sensorData;
  sensorData["type"] = "SENSORS_DATA";
  sensorData["isDoorOpened"] = String(isDoorOpened);
  sensorData["height"] = String(height);
  String jsonSensorData = JSON.stringify(sensorData);
  sendMessage(jsonSensorData);

  if (state != IDLE) {
    setLed(true);

    // Check timeout
    unsigned long processingTimeConsumed = millis() - beginTime;
    if (processingTimeConsumed > TIMEOUT) {
      JSONVar timeoutResponseData;
      if (!requestPost(FRONT_ESP32_URL, "/timeout-esp32-main", "", timeoutResponseData)) return breakLoop();

      sendError("Đã quá thời gian chờ");
      setState(IDLE);

      return breakLoop();
    }
  }

  // Check door
  if (state == IDLE) {
    setLed(false);

    if (isDoorOpened) {
      JSONVar openDoorResponseData;
      if (!requestPost(FRONT_ESP32_URL, "/remind-esp32-main", "", openDoorResponseData)) return breakLoop();

      setState(IDLE);
    }

    beginTime = millis();
    return breakLoop();
  }

  if (state == WAITING_FOR_OPEN_DOOR && isDoorOpened) {
    previousHeight = height;
    setState(OPENING_DOOR);
    beginTime = millis();
    return breakLoop();
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
      // Calculate height
      height = getHeight();
      sumHeight += height;

      delay(100);
    }
    double avgHeight = sumHeight / 5;

    // Submit data to server
    JSONVar submitRequestData;
    submitRequestData["volume"] = avgHeight;
    submitRequestData["embeddedSystemId"] = EMBEDDED_SYSTEM_ID;
    submitRequestData["isCorrect"] = true;

    JSONVar submitResponseData;
    if (!requestPost(SERVER_BASE_URL, "/smart-recycle-bin/submit", JSON.stringify(submitRequestData), submitResponseData)) return breakLoop();

    smartRecycleBinClassificationHistoryId = String(submitResponseData["smartRecycleBinClassificationHistoryId"]);
    String token = submitResponseData["token"];

    JSONVar responseData;
    responseData["type"] = "BUILD_QR";
    responseData["token"] = token;
    String jsonResponseData = JSON.stringify(responseData);
    sendMessage(jsonResponseData);

    setState(CLAIM_REWARD);
    beginTime = millis();
    return breakLoop();
  }

  if (state == CLAIM_REWARD) {
    JSONVar checkClaimRequestData;
    checkClaimRequestData["smartRecycleBinClassificationHistoryId"] = smartRecycleBinClassificationHistoryId;

    JSONVar checkClaimResponseData;
    if (!requestPost(SERVER_BASE_URL, "/smart-recycle-bin/check-claim-reward", JSON.stringify(checkClaimRequestData), checkClaimResponseData)) return breakLoop();
    bool isClaimed = checkClaimResponseData["isClaimed"];
    if (isClaimed) {
      checkClaimResponseData["type"] = "CLAIMED_REWARD";
      String jsonCheckClaimResponseData = JSON.stringify(checkClaimResponseData);
      sendMessage(jsonCheckClaimResponseData);

      setState(REQUESTING_FINISH);
      beginTime = millis();
      return breakLoop();
    }
  }

  if (state == REQUESTING_FINISH) {
    JSONVar openDoorResponseData;
    if (!requestPost(FRONT_ESP32_URL, "/finish-esp32-main", "", openDoorResponseData)) return breakLoop();
    setState(IDLE);
    beginTime = millis();
    return breakLoop();
  }

  return breakLoop();
}