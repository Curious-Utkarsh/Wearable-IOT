#include "WiFi.h"
#include <WebSocketsClient.h>

#define BATCH_SIZE 1
#define MAX_BUFFER_SIZE 256
#define ONBOARD_LED 2

const char* ssid = "NCAIR IOT";
const char* password = "Asim@123Tewari";

const char* ws_host = "10.185.153.66";
const uint16_t ws_port = 1234;
const char* ws_path = "/";

WebSocketsClient webSocket;
bool isConnected = false;

QueueHandle_t uartQueue = NULL;
TaskHandle_t readUARTTask_h = NULL;
TaskHandle_t sendWebSocketTask_h = NULL;

void readUARTTask(void* parameter) {
  char uartData[MAX_BUFFER_SIZE];
  int index = 0;

  Serial.println("[UART Task] Started");

  for (;;) {
    while (Serial1.available()) {
      char rxChar = Serial1.read();
      
      if (rxChar == '{') {
        index = 0;
        uartData[index++] = rxChar;
      } else if (rxChar == '\n') {
        uartData[index++] = rxChar;
        uartData[index] = '\0';

        if (uartQueue != NULL) {
          // Non-blocking send - drop data if queue full
          if (xQueueSend(uartQueue, uartData, 0) == pdTRUE) {
            Serial.printf("[UART] Queued: %s", uartData);
          } else {
            Serial.println("[UART] Queue full - dropping data");
          }
        }
        
        index = 0;
      } else if (index > 0) {
        if (index < MAX_BUFFER_SIZE - 1) {
          uartData[index++] = rxChar;
        } else {
          Serial.println("[UART] Buffer overflow!");
          index = 0;
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void sendWebSocketTask(void* parameter) {
  char uartData[MAX_BUFFER_SIZE];
  
  Serial.println("[WS Task] Started");
  
  for (;;) {
    // Wait for data in queue
    if (xQueueReceive(uartQueue, &uartData, pdMS_TO_TICKS(100))) {
      Serial.printf("[WS Task] Got data: %s", uartData);
      
      // Wait for connection with timeout
      int waitCount = 0;
      while (!isConnected && waitCount < 50) {
        checkWifi();
        webSocket.loop();
        vTaskDelay(pdMS_TO_TICKS(100));
        waitCount++;
        
        if (waitCount % 10 == 0) {
          Serial.printf("[WS Task] Waiting for connection... (%d)\n", waitCount);
        }
      }
      
      if (isConnected) {
        Serial.println("[WS Task] Sending...");
        bool sent = webSocket.sendTXT(uartData);
        
        if (sent) {
          Serial.println("[WS Task] ✓ Sent successfully");
        } else {
          Serial.println("[WS Task] ✗ Send failed");
        }
      } else {
        Serial.println("[WS Task] ✗ Timeout - not connected");
      }
    }
    
    // Keep WebSocket alive
    webSocket.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== ESP32 WebSocket Client ===");
  
  Serial1.begin(115200, SERIAL_8N1, 16, 17);
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, LOW);

  // Create queue (reduced size to prevent buildup)
  uartQueue = xQueueCreate(20, MAX_BUFFER_SIZE * sizeof(char));
  if (uartQueue == NULL) {
    Serial.println("[Setup] ✗ Queue creation failed!");
    while(1);
  }
  Serial.println("[Setup] ✓ Queue created");

  // Connect to WiFi
  Serial.println("[Setup] Connecting to WiFi...");
  init_wifi();
  Serial.print("[Setup] ✓ WiFi connected! IP: ");
  Serial.println(WiFi.localIP());

  // Initialize WebSocket
  Serial.printf("[Setup] Connecting to ws://%s:%d%s\n", ws_host, ws_port, ws_path);
  webSocket.begin(ws_host, ws_port, ws_path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  
  // Enable heartbeat to keep connection alive
  webSocket.enableHeartbeat(15000, 3000, 2);

  // Create tasks
  xTaskCreatePinnedToCore(readUARTTask, "Read UART", 4096, NULL, 1, &readUARTTask_h, 0);
  xTaskCreatePinnedToCore(sendWebSocketTask, "Send WebSocket", 8192, NULL, 1, &sendWebSocketTask_h, 1);

  Serial.println("[Setup] ✓ Setup complete!");
  digitalWrite(ONBOARD_LED, HIGH);
}

void loop() {
  webSocket.loop();
  vTaskDelay(pdMS_TO_TICKS(1));
}

void init_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
    attempts++;
    
    if (attempts > 60) {
      Serial.println("\n[WiFi] ✗ Failed - restarting");
      ESP.restart();
    }
  }
  Serial.println();
}

void checkWifi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Connection lost!");
    digitalWrite(ONBOARD_LED, LOW);
    isConnected = false;
    
    WiFi.reconnect();
    int attempts = 0;
    
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      attempts++;
      vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WiFi] ✓ Reconnected");
      webSocket.begin(ws_host, ws_port, ws_path);
    }
  }
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS Event] ✗ Disconnected");
      isConnected = false;
      digitalWrite(ONBOARD_LED, LOW);
      break;
      
    case WStype_CONNECTED:
      Serial.printf("[WS Event] ✓ Connected to: %s\n", payload);
      isConnected = true;
      digitalWrite(ONBOARD_LED, HIGH);
      break;
      
    case WStype_TEXT:
      Serial.printf("[WS Event] ← Received: %s\n", payload);
      break;
      
    case WStype_ERROR:
      Serial.printf("[WS Event] ✗ Error: %s\n", payload);
      isConnected = false;
      break;
      
    case WStype_PING:
      Serial.println("[WS Event] ← Ping");
      break;
      
    case WStype_PONG:
      Serial.println("[WS Event] ← Pong");
      break;
      
    default:
      break;
  }
}
