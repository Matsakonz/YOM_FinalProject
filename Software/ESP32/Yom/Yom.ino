#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "SPI.h"
#include "TFT_22_ILI9225.h"
#include "images.h"
#include "TetrisGame.h"

#define TFT_RST 26  
#define TFT_RS  25  
#define TFT_CLK 14 
#define TFT_SDA 13  
#define TFT_CS  15  
#define TFT_LED 0   
#define TFT_BRIGHTNESS 200 

#define RX_PIN 16
#define TX_PIN 17

#define POT_PIN  34 
#define BTN_PIN  33

#define GRAPH_X 15
#define GRAPH_Y 140
#define GRAPH_W 150 
#define GRAPH_H 65  
#define GRAPH_COLOR_STM COLOR_BLUE
#define GRAPH_COLOR_POT COLOR_RED

SPIClass hspi(HSPI);
TFT_22_ILI9225 tft = TFT_22_ILI9225(TFT_RST, TFT_RS, TFT_CS, TFT_LED, TFT_BRIGHTNESS);

#define ESP32

HardwareSerial Uart(2);

// --- MQTT Configuration ---
const char* mqtt_broker = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_pass = "";

const char* topic_publish = "YOM/sensors";
const char* topic_subscribe = "YOM/statusplant";

const char* ssid = "._.";
const char* password = "01345280";

WiFiClient tcpClient;
PubSubClient mqttClient(tcpClient);

// --- Timing and State Variables ---
uint32_t Previous_Tick = 0, Current_Tick = 0;
uint32_t Loop_Period = 100;
bool new_data_ready = false;
int state = 0;
int old_state = 0; 

// --- Sensor Variables ---
uint8_t mode = 0;
uint8_t pre_mode = 0;
uint16_t adc = 0;
uint16_t moist = 0;
uint16_t light = 0;
uint16_t temp = 0;
uint16_t pres = 0;
uint16_t humi = 0;

// --- UART Buffer ---
const int PACKET_SIZE = 16;
uint8_t rx_buffer[PACKET_SIZE];
int rx_index = 0;
uint32_t last_packet_time = 0;

// --- Rapid Press Detection ---
TetrisGame game;
bool tetrisMode = false;
int modeChangeCount = 0;
uint32_t lastModeChangeTime = 0;
bool lastBtnState = HIGH;

// ==========================================
// WIFI SETUP
// ==========================================
void setup_wifi(const char* ssid, const char* password) {
  Serial.println("Connecting to: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\r\nWiFi Connected");
  Serial.println("IP Address: ");
  Serial.println(WiFi.localIP());
}

// ==========================================
// MQTT CALLBACK (When data is received)
// ==========================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (String(topic) == topic_subscribe) {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error) {
      Serial.print("JSON Parse failed: ");
      Serial.println(error.f_str());
      return;
    }

    const char* status_humidity = doc["humidity"];
    const char* status_light = doc["light"];
    const char* status_temp = doc["temperature"];
    
    int happyCount = 0;

    if (strcmp(status_light, "happy") != 0) {
      happyCount++;
    }
    if (strcmp(status_temp, "happy") != 0) {
      happyCount++;
    }
    if (strcmp(status_humidity, "happy") != 0) {
      happyCount++;
    }

    state = happyCount; 
    Uart.println(state);

    // Serial.printf("Happy Score: %d | Hum: %s, Light: %s, Temp: %s\n", 
    //               state, status_humidity, status_light, status_temp);
  }
}

// ==========================================
// MQTT RECONNECT FUNCTION
// ==========================================
void reconnectMQTT() {
  if (!mqttClient.connected()) {
    Serial.print("MQTT connection...");

    String clientId = "ESP32";
    clientId += String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("Connected!");
      mqttClient.subscribe(topic_subscribe);
    } else {
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" - Will try again next loop.");
    }
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  Uart.begin(115200);

  setup_wifi(ssid,password);
  mqttClient.setServer(mqtt_broker, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  hspi.begin();
  tft.begin(hspi);
  tft.setOrientation(3);
  tft.clear();
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      reconnectMQTT();
    } else {
      mqttClient.loop();
    }
  }

  Receive();

  if (new_data_ready && mqttClient.connected()) {
    StaticJsonDocument<200> doc;
    int moisturePercent = getMoisturePercent(moist);
    int Lux = getLuxHighRange(light);

    doc["temp"] = temp;
    doc["humidity"] = moisturePercent;
    doc["light"] = Lux;

    char jsonBuffer[256];
    serializeJson(doc, jsonBuffer);

    // Publish to the topic
    mqttClient.publish(topic_publish, jsonBuffer);
    // Serial.print("Published MQTT: ");
    // Serial.println(jsonBuffer);

    new_data_ready = false;
  }

  Current_Tick = millis();
  if (Current_Tick - Previous_Tick >= Loop_Period) {
    Previous_Tick = Current_Tick;
    mqttClient.publish("YOM/heartbeat", "connect");
    updateScreen();
  }
}

// ==========================================
// UART RECEIVE & PARSE
// ==========================================
void Receive() {
  while (Uart.available()) {
    uint8_t data = Uart.read();

    if (rx_index == 0 && data != 0xAA) {
      continue;
    }

    if (rx_index == 1 && data != 0x55) {
      rx_index = 0;
      continue;
    }

    rx_buffer[rx_index] = data;
    rx_index++;

    if (rx_index == PACKET_SIZE) {
      uint8_t calc_chk = 0;
      for (int i = 2; i < 15; i++) {
        calc_chk ^= rx_buffer[i];
      }

      if (calc_chk == rx_buffer[15]) {
        mode = rx_buffer[2];
        adc = (rx_buffer[3] << 8) | rx_buffer[4];
        moist = (rx_buffer[5] << 8) | rx_buffer[6];
        light = (rx_buffer[7] << 8) | rx_buffer[8];
        temp = (rx_buffer[9] << 8) | rx_buffer[10];
        pres = (rx_buffer[11] << 8) | rx_buffer[12];
        humi = (rx_buffer[13] << 8) | rx_buffer[14];

        Serial.printf("Valid Packet! Mode: %d, Light: %d, Temp: %d, Humi: %d\n", mode, light, adc, moist);

      if (mode != pre_mode) {
          if (tetrisMode) {
              int nextRot = (game.pRot + 1) % 4;
              if (!game.checkCollision(game.px, game.py, nextRot)) {
                  game.pRot = nextRot;
              }
          } else {
              tft.clear();
          }

          uint32_t now = millis();
          if (now - lastModeChangeTime < 1500) { 
              modeChangeCount++;
          } else {
              modeChangeCount = 1;
          }
          lastModeChangeTime = now;

          if (modeChangeCount >= 5) {
              tetrisMode = !tetrisMode;
              modeChangeCount = 0;
              tft.clear();
              if (tetrisMode) game.init();
          }
      }

      pre_mode = mode;
      new_data_ready = true;
      last_packet_time = millis();

      } else {
        Serial.println("Packet dropped: Checksum mismatch!");
      }
      rx_index = 0;
    }
  }
}

// ==========================================
// MOSTURE PERCENT
// ==========================================
int getMoisturePercent(int rawValue) {
  const int DRY_VAL = 185;
  const int WET_VAL = 487;

  int percentage = map(rawValue, DRY_VAL, WET_VAL, 0, 100);

  percentage = constrain(percentage, 0, 100);

  return percentage;
}

// ==========================================
// LUX CONVERTS
// ==========================================
float getLuxHighRange(int rawValue) {
  float lux;

  if (rawValue > 1400) {
    lux = map(rawValue, 6700, 1400, 0, 130);
  } else {
    lux = map(rawValue, 1400, 150, 130, 800);
  }

  return constrain(lux, 0, 1000);
}

// ==========================================
// UPDATE SCREEN
// ==========================================
void updateScreen(){
  if (tetrisMode) {
    int potVal = adc;
    int targetX = map(potVal, 10, 4080, 0, GRID_W - 3); 
    
    if (!game.checkCollision(targetX, game.py, game.pRot)) {
        game.px = targetX;
    }

    if (millis() - game.lastFall > 600) {
        if (!game.checkCollision(game.px, game.py + 1, game.pRot)) {
            game.py++;
        } else {
            game.lockPiece(); 
            
            if (game.checkCollision(game.px, game.py, game.pRot)) {
                game.init();
            }
        }
        game.lastFall = millis();
    }

    game.draw(tft);
    tft.setFont(Terminal6x8);
    tft.drawText(130, 20, "SCORE:", COLOR_WHITE);
    tft.drawText(130, 35, String(game.score), COLOR_YELLOW);
    
  }else if (mode == 1) {
    tft.setBackgroundColor(COLOR_BLACK);
    tft.setFont(Terminal11x16);

    if (millis() - last_packet_time < 3000) {
      tft.drawText(10, 5, "LINK: OK", COLOR_GREEN);
    } else {
      tft.drawText(10, 5, "LINK: LOST", COLOR_RED); 
      tft.fillRectangle(10, 5, 100, 22, COLOR_BLACK);
    }

    tft.drawRectangle(5, 25, 170, 215, COLOR_WHITE);

    tft.setFont(Terminal12x16);
    int xL = 15;
    int xV = 85;
    
    tft.drawText(xL, 45, "Temp:", COLOR_WHITE);
    tft.drawText(xV, 45, String(temp) + "C", COLOR_CYAN);

    tft.drawText(xL, 70, "Pres:", COLOR_WHITE);
    tft.drawText(xV, 70, String(pres), COLOR_CYAN);

    tft.drawText(xL, 95, "Humi:", COLOR_WHITE);
    tft.drawText(xV, 95, String(humi) + "%", COLOR_CYAN);

    int moisturePercent = getMoisturePercent(moist);
    tft.drawText(xL, 120, "Soil:", COLOR_WHITE);
    tft.drawText(xV, 120, String(moisturePercent) + "%", COLOR_GREEN);

    float LuxVal = getLuxHighRange(light);
    tft.drawText(xL, 145, "Light:", COLOR_WHITE);
    tft.drawText(xV, 145, String((int)LuxVal), COLOR_YELLOW);

    uint16_t statusColor = (mode == 3) ? COLOR_GREEN : (mode >= 1 ? COLOR_YELLOW : COLOR_RED);
    tft.fillRectangle(10, 185, 165, 210, statusColor);
    tft.drawText(60, 190, "STATUS", COLOR_BLACK);
  }else {
    if (state != old_state){
      tft.clear();
    }
    old_state = state;
    if (state == 0){
      tft.drawBitmap(0, 0, epd_bitmap_allArray[1], 220, 180, COLOR_GREEN);
      
    }else if (state == 1){
      tft.drawBitmap(0, 0, epd_bitmap_allArray[3], 220, 180, COLOR_GREEN);
    }else if (state == 2){
      tft.drawBitmap(0, 0, epd_bitmap_allArray[2], 220, 180, COLOR_GREEN);
    }else{
      tft.drawBitmap(0, 0, epd_bitmap_allArray[0], 220, 180, COLOR_GREEN);
    }
  }
}