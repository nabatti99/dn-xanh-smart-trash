#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Arduino_JSON.h>
#include <HTTPClient.h>

// Define pins
#define TRIG_PIN 5
#define ECHO_PIN 18

// Constants
const String EMBEDDED_SYSTEM_FRONT_ID = "DN-SMT-001_FRONT";  // Change it
enum State { IDLE,
             COLLECTING_DATA,
             REQUESTING_OPEN_DOOR,
             WAITING_ESP32_MAIN,
             REMINDING,
             THANKS };
const unsigned long TIMEOUT = 30000;
const String SERVER_BASE_URL = "http://api-dnx.passgenix.com/api";
const String CAMERA_BASE_URL = "http://192.168.100.101";
const String ORGANIC_ESP32_URL = "http://192.168.100.20";
const String RECYCLABLE_ESP32_URL = "http://192.168.100.30";
const String NON_RECYCLABLE_ESP32_URL = "http://192.168.100.40";

// Wifi config
const char *SSID = "MERCUSYS_DCE1";
const char *PASSWORD = "123456789";

// Static IP config
IPAddress localIP(192, 168, 100, 100);
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

    case COLLECTING_DATA:
      return "COLLECTING_DATA";

    case REQUESTING_OPEN_DOOR:
      return "REQUESTING_OPEN_DOOR";

    case WAITING_ESP32_MAIN:
      return "WAITING_ESP32_MAIN";

    case REMINDING:
      return "REMINDING";

    case THANKS:
      return "THANKS";
  }
}

String wasteTypeToEsp32Url(String wasteType) {
  if (wasteType == "ORGANIC")
    return ORGANIC_ESP32_URL;

  if (wasteType == "RECYCLABLE")
    return RECYCLABLE_ESP32_URL;

  if (wasteType == "NON_RECYCLABLE")
    return NON_RECYCLABLE_ESP32_URL;
}

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("Connected to AP successfully!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("WiFi connected");
  isConnectedToWifi = true;

  Serial.print("IP address: ");
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

void initInfraredSensor() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
}

void setup() {
  Serial.begin(115200);
  initWiFi();
  initWebSocket();
  initInfraredSensor();

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "DN Xanh Embedded System Front: " + EMBEDDED_SYSTEM_FRONT_ID);
  });

  server.on("/timeout-esp32-main", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(201, "text/plain", "{\"message\":\"OK\"}");
    setState(IDLE);
  });

  server.on("/remind-esp32-main", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(201, "text/plain", "{\"message\":\"OK\"}");
    setState(REMINDING);
  });

  server.on("/finish-esp32-main", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(201, "text/plain", "{\"message\":\"OK\"}");
    setState(THANKS);
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

void sendError(const String error) {
  JSONVar responseData;
  responseData["type"] = "ERROR";
  responseData["message"] = error;
  String jsonresponseData = JSON.stringify(responseData);
  sendMessage(jsonresponseData);
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
      sendError("Kết nối camera không thành công");
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
  String jsonresponseData = JSON.stringify(responseData);
  sendMessage(jsonresponseData);
}

bool checkInfraredSensor() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);
  float distance = duration * 0.034 / 2; 
  if (distance > 10) return false; // No object detected
  return true;
}

void breakLoop() {
  // Limiting the number of web socket clients
  ws.cleanupClients();
  delay(1000);
}

void loop() {
  static unsigned long beginTime = 0;
  static String esp32Url;

  // Verify wifi connection
  if (!isConnectedToWifi) return breakLoop();
  
  // Verify websocket connection
  if (currentClientId == 0) {
    Serial.println("Waiting for client connect...");
    state = IDLE;

    return breakLoop();
  }

  // Get sensor data
  bool hasObject = checkInfraredSensor();

  // Send sensors data to monitor
  JSONVar sensorData;
  sensorData["type"] = "SENSORS_DATA";
  sensorData["hasObject"] = String(hasObject);
  String jsonSensorData = JSON.stringify(sensorData);
  sendMessage(jsonSensorData);

  // Check door
  if (hasObject && state == IDLE) {
    setState(COLLECTING_DATA);
    beginTime = millis();
    return breakLoop();
  }

  if (state == IDLE) {
    beginTime = millis();
    return breakLoop();
  }

  if (state != IDLE) {
    // Check timeout
    unsigned long processingTimeConsumed = millis() - beginTime;

    unsigned long timeout = TIMEOUT;
    if (state == WAITING_ESP32_MAIN) {
      timeout = TIMEOUT * 2;
    }

    if (processingTimeConsumed > timeout) {
      sendError("Đã quá thời gian chờ");
      setState(IDLE);

      return breakLoop();
    }
  }

  if (state == COLLECTING_DATA) {
    String wasteTypePredictions[3];
    int nilonBagCount = 0;
    JSONVar wasteTypePredictionsCount;
    for (int i = 0; i < 3; ++i) {
      // Capture & classify image
      JSONVar captureAndClassifyResponseData;
      if (!requestGet(CAMERA_BASE_URL, "/capture-and-classify", captureAndClassifyResponseData)) return breakLoop();

      // if (!captureAndClassifyResponseData["shouldClassification"]) {
      //   JSONVar warningData;
      //   warningData["type"] = "SENSORS_DATA";
      //   warningData["hasObject"] = String(hasObject);
      //   String jsonWarningData = JSON.stringify(warningData);
      //   sendMessage(jsonWarningData);
      // }

      String wasteTypePrediction = captureAndClassifyResponseData["wasteTypePrediction"];

      wasteTypePredictions[i] = wasteTypePrediction;
      if (JSON.typeof(wasteTypePredictionsCount[wasteTypePrediction]) == "undefined") wasteTypePredictionsCount[wasteTypePrediction] = 1;
      else wasteTypePredictionsCount[wasteTypePrediction] = ((int)wasteTypePredictionsCount[wasteTypePrediction]) + 1;

      String prediction = captureAndClassifyResponseData["prediction"];
      Serial.printf("Prediction: %s\n", prediction.c_str());
      if (prediction == "nilon-bag") {
        nilonBagCount += 1;
      }
    }

    // Check nilon bag
    if (nilonBagCount >= 2) {
      sendError("Không nên bỏ tất cả rác vào một túi!");
      setState(IDLE);
      return breakLoop();
    }

    // Get highest wasteType prediction
    String highestWasteTypePrediction;
    for (int i = 0; i < 3; ++i) {
      String wasteTypePrediction = wasteTypePredictions[i];
      if ((int)wasteTypePredictionsCount[wasteTypePrediction] > (int)wasteTypePredictionsCount[highestWasteTypePrediction])
        highestWasteTypePrediction = wasteTypePrediction;
    }
    Serial.printf("Kết quả phân loại rác: %s\n", highestWasteTypePrediction.c_str());

    // Prepare ESP32 Main URL for calling
    esp32Url = wasteTypeToEsp32Url(highestWasteTypePrediction);
    // Serial.println(esp32Url);

    // setState(IDLE);

    setState(REQUESTING_OPEN_DOOR);
    beginTime = millis();
    return breakLoop();
  }

  if (state == REQUESTING_OPEN_DOOR) {
    JSONVar openDoorResponseData;
    if (!requestPost(esp32Url, "/request-open-door", "", openDoorResponseData)) return breakLoop();
    setState(WAITING_ESP32_MAIN);
    beginTime = millis();
    return breakLoop();
  }

  if (state == WAITING_ESP32_MAIN) {
    Serial.println("Waiting for ESP32 main...");
    return breakLoop();
  }

  if (state == REMINDING) {
    Serial.println("Reminding ...");
    delay(4000);
    setState(IDLE);
    return breakLoop();
  }

  if (state == THANKS) {
    Serial.println("Finishing session ...");
    delay(4000);
    setState(IDLE);
    return breakLoop();
  }

  return breakLoop();
}