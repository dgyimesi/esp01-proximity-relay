#include <ESP8266WiFi.h>
#include <MKL_HCSR04.h>

// Debug: setting to false enables RX/TX as relay I/O, otherwise RX/TX is used for serial monitor.
#define DEBUG false

#if DEBUG == true
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

// WiFi
#define WIFI_SSID "v3rys3cr3t"
#define WIFI_PASSWORD "v3rys3cr3t"
#define WIFI_HTTP_PORT 80
#define WIFI_STATUS_CHECK_INTERVAL 1000

// Relay
#define RELAY_PIN 0
#define RELAY_STATE_DEFAULT HIGH
#define RELAY_MODULE_NAME "Bathroom Mirror Light"

// Measurement
// NOTE: each measurement can take ~25ms + 75ms delay added to prevent trigger to echo leakage.
// Use multiple measurements (DISTANCE_MEASUREMENT_COUNT > 1) if you experience inaccuracies.
// Otherwise go with 250ms (DISTANCE_MEASUREMENT_INTERVAL_MS) to get a nice responsivity.
#define DISTANCE_MEASUREMENT_COUNT 3
#define DISTANCE_MEASUREMENT_INTERVAL_MS 0
#define DISTANCE_TRIGGER_ZONE_CM 25
#define DISTANCE_SENSOR_PIN_TRIGGER 1
#define DISTANCE_SENSOR_PIN_ECHO 3

// Prepare for var!
unsigned long timeNow = 0;
unsigned long timeSinceLastTrigger = 0;
unsigned long timeReportDistance = 0;

int relayState = RELAY_STATE_DEFAULT;

bool isApiRequest = false;
bool isLocked = false;

float distance = 0;
float lastMeasuredDistance = 0;
float lastTriggerDistance = 0;

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
  // /api: returns relay state, measured distance (cm), seconds since last trigger and last trigger distance (cm).
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
    delay(1000);

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
    distance = 0;
    
    // Doing average because reported distance tends to fluctuate no matter the delay set between measurements.
    unsigned long timeMeasurementStart = millis();
    for (int i = 0; i < DISTANCE_MEASUREMENT_COUNT; i++) {
      distance += hc.dist();

      delay(75);
    }
    unsigned long timeMeasurementEnd = millis();
    
    DEBUG_PRINT("Measurement took ");
    DEBUG_PRINT(timeMeasurementEnd - timeMeasurementStart);
    DEBUG_PRINTLN(" ms");

    distance /= DISTANCE_MEASUREMENT_COUNT;
    
    bool approaching = distance - lastMeasuredDistance < 0;
    bool receding = distance - lastMeasuredDistance > 0;

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

      lastTriggerDistance = distance;
      timeSinceLastTrigger = timeNow;
    }

    lastMeasuredDistance = distance;

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
    delay(1);
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

    timeSinceLastTrigger = timeNow;

    digitalWrite(RELAY_PIN, relayState);
  }

  if (request.indexOf("/off") != -1) {
    DEBUG_PRINTLN("Turning relay off");

    relayState = HIGH;

    timeSinceLastTrigger = timeNow;

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
    client.print(relayState == LOW ? "true" : "false");
    client.print(", \"distance\": ");
    client.print(distance);
    client.print(", \"lastTriggerDistance\": ");
    client.print(lastTriggerDistance);
    client.print(", \"timeSinceLastTrigger\": ");
    client.print((int)((timeNow - timeSinceLastTrigger) / 1000));
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