#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

const char* ssid = "ESP8266_Locator";
const char* password = "12345678";


const int ANCHOR_ID = 1;

const char* serverAddress = "http://192.168.4.1";

const int RSSI_SAMPLES = 5;
float rssi_values[RSSI_SAMPLES];
int rssi_array_index = 0;

#define DATA_PIN  23  
#define CS_PIN    5   
#define CLK_PIN   18  

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 1

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

const byte circle[] = {
    B00111100,
    B01111110,
    B11111111,
    B11100111,
    B11100111,
    B11111111,
    B01111110,
    B00111100
};

void setupLEDMatrix() {
    mx.begin();
    mx.control(MD_MAX72XX::INTENSITY, 8);
    mx.clear();
}

void displayCircle() {
    for (int row = 0; row < 8; row++) {
        mx.setRow(0, row, circle[row]);
    }
}

void clearDisplay() {
    mx.clear();
}

void blinkCircle() {
    for (int i = 0; i < 3; i++) {  // Blink 3 times
        displayCircle();
        delay(500);
        clearDisplay();
        delay(500);
    }
    displayCircle();  // Leave the circle displayed
}

void checkButtonState() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = String(serverAddress) + "/button_state";
        
        http.begin(url);
        int httpResponseCode = http.GET();
        
        if (httpResponseCode > 0) {
            String response = http.getString();
            StaticJsonDocument<200> doc;
            DeserializationError error = deserializeJson(doc, response);
            
            if (!error) {
                bool buttonPressed = doc["pressed"];
                if (buttonPressed) {
                    Serial.println("Button press detected! Blinking circle...");
                    blinkCircle();
                }
            }
        }
        
        http.end();
    }
}

void setup() {
    Serial.begin(115200);
    
    Serial.println("\n=== ESP32 Anchor Node ===");
    Serial.printf("Anchor ID: %d\n", ANCHOR_ID);
    
    setupLEDMatrix();
    displayCircle();  
    
    
    for(int i = 0; i < RSSI_SAMPLES; i++) {
        rssi_values[i] = 0;
    }
    
   
    Serial.printf("Connecting to %s\n", ssid);
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("\nConnected to ESP8266 AP");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

float getAverageRSSI() {
   
    float current_rssi = WiFi.RSSI();
    rssi_values[rssi_array_index] = current_rssi;
    rssi_array_index = (rssi_array_index + 1) % RSSI_SAMPLES;
    
    
    float sum = 0;
    for(int i = 0; i < RSSI_SAMPLES; i++) {
        sum += rssi_values[i];
    }
    float avg_rssi = sum / RSSI_SAMPLES;
    
   
    Serial.printf("Current RSSI: %.1f dBm, Average RSSI: %.1f dBm\n", current_rssi, avg_rssi);
    
    return avg_rssi;
}

void sendRSSI() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        
       
        float rssi = getAverageRSSI();
        
        StaticJsonDocument<200> doc;
        doc["anchor_id"] = ANCHOR_ID;
        doc["rssi"] = rssi;
        
        String jsonString;
        serializeJson(doc, jsonString);
        
        // Send POST request to ESP8266
        String url = String(serverAddress) + "/update";
        Serial.printf("Sending update to: %s\n", url.c_str());
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        
        int httpResponseCode = http.POST(jsonString);
        
        if (httpResponseCode > 0) {
            Serial.printf("HTTP Response code: %d\n", httpResponseCode);
            String response = http.getString();
            Serial.println(response);
        } else {
            Serial.printf("Error sending update: %d\n", httpResponseCode);
        }
        
        http.end();
    } else {
        Serial.println("WiFi Disconnected! Attempting to reconnect...");
        WiFi.begin(ssid, password);
    }
}

void loop() {
    sendRSSI();
    checkButtonState();  
    delay(1000);
}
