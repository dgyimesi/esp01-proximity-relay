#include <ESP8266WiFi.h>
#include <MKL_HCSR04.h>

#define DEBUG false

#if DEBUG == true
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

#define WIFI_SSID "v3rys3cr3t"
#define WIFI_PASSWORD "v3rys3cr3t"
#define WIFI_HTTP_PORT 80
#define WIFI_STATUS_CHECK_INTERVAL 1000

#define RELAY_PIN 0
#define RELAY_STATE_DEFAULT HIGH
#define RELAY_MODULE_NAME "Bathroom Mirror Light"

#define DISTANCE_MEASUREMENT_INTERVAL_MS 250
#define DISTANCE_TRIGGER_ZONE_CM 25
#define DISTANCE_SENSOR_PIN_TRIGGER 1
#define DISTANCE_SENSOR_PIN_ECHO 3

unsigned long timeNow = 0;
unsigned long timeReportDistance = 0;

int relayState = RELAY_STATE_DEFAULT;

bool isApiRequest = false;
bool isLocked = false;

float distance = 0;
float lastDistance = 0;

WiFiServer server(WIFI_HTTP_PORT);
MKL_HCSR04 hc(DISTANCE_SENSOR_PIN_TRIGGER, DISTANCE_SENSOR_PIN_ECHO);

void setup() {
  // Setting up GPIO1/3.
  if (DEBUG == true) {
    // In debug mode we use RX/TX for Serial comms.
    Serial.begin(115200);
  } else {
    // Re-assign GPIO3/RX as input (echo).
    pinMode(3, INPUT);

    // Re-assign GPIO1/TX as output (trigger).
    pinMode(1, OUTPUT);
  }

  // Init status LED.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Init relay.
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_STATE_DEFAULT);

  // Setup Wifi and HTTP server.
  setupWifi();
}

void loop() {
  timeNow = millis();

  // Evaluating distance from sensor.
  checkDistance();

  // /off: switches the relay OFF.
  // /on:  switches the relay ON.
  // /api: returns relay state and measured distance in cms.
  handleRequests();
}

void setupWifi() {
  DEBUG_PRINT("Connecting to ");
  DEBUG_PRINT(WIFI_SSID);

  WiFi.hostname(RELAY_MODULE_NAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);

    DEBUG_PRINT(".");
  }

  DEBUG_PRINTLN();

  switch (WiFi.status()) {
    case WL_NO_SSID_AVAIL:
      DEBUG_PRINTLN("Configured SSID cannot be reached");

      digitalWrite(LED_BUILTIN, HIGH);

      break;
    case WL_CONNECTED:
      DEBUG_PRINTLN("Connection successfully established");

      digitalWrite(LED_BUILTIN, LOW);

      break;
    case WL_CONNECT_FAILED:
      DEBUG_PRINTLN("Connection failed");

      digitalWrite(LED_BUILTIN, HIGH);

      break;
  }

  // Start HTTP server.
  server.begin();

  DEBUG_PRINT("Listening on ");
  DEBUG_PRINTLN(WiFi.localIP());

  digitalWrite(LED_BUILTIN, LOW);
}

void checkDistance() {
  if (timeNow - timeReportDistance >= DISTANCE_MEASUREMENT_INTERVAL_MS) {
    distance = hc.dist();

    bool approaching = distance - lastDistance < 0;
    bool receding = distance - lastDistance > 0;

    // Check if toggle isLocked can be opened (hand left the trigger zone).
    if (isLocked) {
      if (distance > DISTANCE_TRIGGER_ZONE_CM) {
        isLocked = false;
      }
    }

    // Hand approches trigger zone.
    if (!isLocked && approaching && distance < DISTANCE_TRIGGER_ZONE_CM) {
      relayState = 1 - relayState;

      // isLocked state until hand left is in the trigger zone [0cm, DISTANCE_TRIGGER_ZONE_CM].
      isLocked = true;
    }

    lastDistance = distance;

    timeReportDistance = timeNow;

    digitalWrite(RELAY_PIN, relayState);
  }
}

void handleRequests() {
  // Check if a client has connected.
  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  // Wait until the client sends some data.
  while (!client.available()) {
    delay(100);
  }

  DEBUG_PRINTLN("New client");

  isApiRequest = false;

  // Read the first line of the request.
  String request = client.readStringUntil('\r');
  DEBUG_PRINTLN(request);

  if (request.indexOf("/api") != -1) {
    DEBUG_PRINTLN("API request");

    isApiRequest = true;
  }

  if (request.indexOf("/on") != -1) {
    DEBUG_PRINTLN("Turning relay on");

    relayState = LOW;
    digitalWrite(RELAY_PIN, relayState);
  }

  if (request.indexOf("/off") != -1) {
    DEBUG_PRINTLN("Turning relay off");

    relayState = HIGH;
    digitalWrite(RELAY_PIN, relayState);
  }

  // Return the response.
  client.println("HTTP/1.1 200 OK");
  client.print("Content-Type: ");
  client.println(isApiRequest == true ? "application/json" : "text/html");
  client.println("Connection: close");
  client.println("");

  if (isApiRequest) {
    client.print("{ \"relayState\": ");
    client.print(relayState == HIGH ? "true" : "false");
    client.print(", \"distance\": ");
    client.print(distance);
    client.print(" }");
  } else {
    client.println("<!DOCTYPE HTML>");
    client.println("<html style='background: black; width: 100%;'>");
    client.println("<head><meta name='viewport' content='width=360'><title>");
    client.println(RELAY_MODULE_NAME);
    client.println("</title></head>");

    if (relayState == HIGH) {
      client.println("<a style='display: grid; margin: 10px auto; padding: 24px; align-content: space-evenly; font-family: Arial; font-size: 48px; border: 2px solid #fff; text-align: center; color: #fff; text-decoration: none; background: #666;' href='/off'>OFF</a>");
      client.println("<a style='display: grid; margin: 10px auto; padding: 24px; align-content: space-evenly; font-family: Arial; font-size: 48px; border: 2px solid #999; text-align: center; color: #999; text-decoration: none; background: #333;' href='/on'>ON</a>");
    } else {
      client.println("<a style='display: grid; margin: 10px auto; padding: 24px; align-content: space-evenly; font-family: Arial; font-size: 48px; border: 2px solid #999; text-align: center; color: #999; text-decoration: none; background: #333;' href='/off'>OFF</a>");
      client.println("<a style='display: grid; margin: 10px auto; padding: 24px; align-content: space-evenly; font-family: Arial; font-size: 48px; border: 2px solid #fff; text-align: center; color: #fff; text-decoration: none; background: #666;' href='/on'>ON</a>");
    }

    client.println("</html>");
    client.flush();
  }
}