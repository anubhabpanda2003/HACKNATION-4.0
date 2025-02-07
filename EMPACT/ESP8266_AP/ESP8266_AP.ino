#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>

const char* ssid = "ESP8266_Locator";
const char* password = "12345678";

ESP8266WebServer server(80);

// Store RSSI values from anchors
float rssi_anchor1 = 0;
float rssi_anchor2 = 0;


float pos_x = 50;  
float pos_y = 50;  


const float REF_RSSI = -50.0;  
const float PATH_LOSS_EXPONENT = 2.7; 
const float KALMAN_Q = 0.1;    
const float KALMAN_R = 0.1;   

// Kalman filter variables for each anchor
float kalman_p1 = 1.0;
float kalman_p2 = 1.0;
float kalman_x1 = 0;
float kalman_x2 = 0;
bool kalman_init1 = false;
bool kalman_init2 = false;


const float ANCHOR1_X = 25;  // 25cm from left
const float ANCHOR1_Y = 25;  // 25cm from top
const float ANCHOR2_X = 75;  // 75cm from left
const float ANCHOR2_Y = 25;  // 25cm from top


const int BUTTON_PIN = D5;  
bool lastButtonState = HIGH;
bool buttonPressed = false;

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP8266 Location Tracker</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        canvas {
            border: 2px solid black;
            margin: 20px auto;
            display: block;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
            padding: 20px;
            text-align: center;
            font-family: Arial, sans-serif;
        }
        .info {
            margin: 10px;
            padding: 10px;
            background-color: #f0f0f0;
            border-radius: 5px;
        }
        h1 {
            color: #333;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP8266 Location Tracker</h1>
        <canvas id="locationCanvas" width="400" height="400"></canvas>
        <div class="info">
            <div id="coordinates">Position: Calculating...</div>
            <div id="rssi">RSSI Values: Waiting for data...</div>
        </div>
    </div>
    <script>
        const canvas = document.getElementById('locationCanvas');
        const ctx = canvas.getContext('2d');
        const coordsDiv = document.getElementById('coordinates');
        const rssiDiv = document.getElementById('rssi');

        function drawPoint(x, y, rssi1, rssi2) {
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            
            // Draw grid
            ctx.strokeStyle = '#ddd';
            for(let i = 0; i <= canvas.width; i += 40) {
                ctx.beginPath();
                ctx.moveTo(i, 0);
                ctx.lineTo(i, canvas.height);
                ctx.stroke();
                ctx.beginPath();
                ctx.moveTo(0, i);
                ctx.lineTo(canvas.width, i);
                ctx.stroke();
            }

            // Draw anchors
            ctx.fillStyle = 'blue';
            ctx.beginPath();
            ctx.arc(100, 100, 8, 0, 2 * Math.PI);
            ctx.fill();
            ctx.fillText("Anchor 1", 85, 85);
            
            ctx.beginPath();
            ctx.arc(300, 100, 8, 0, 2 * Math.PI);
            ctx.fill();
            ctx.fillText("Anchor 2", 285, 85);

            // Draw device location
            if (x >= 0 && y >= 0) {
                ctx.fillStyle = 'red';
                ctx.beginPath();
                ctx.arc(x * 4, y * 4, 8, 0, 2 * Math.PI);
                ctx.fill();
                
                coordsDiv.textContent = `Position: (${x.toFixed(1)}cm, ${y.toFixed(1)}cm)`;
                rssiDiv.textContent = `RSSI Values: Anchor1: ${rssi1}dBm, Anchor2: ${rssi2}dBm`;
            }
        }

        function updateLocation() {
            fetch('/position')
                .then(response => response.json())
                .then(data => {
                    if (data.x >= 0 && data.y >= 0) {
                        drawPoint(data.x, data.y, data.rssi1, data.rssi2);
                    }
                })
                .catch(error => console.error('Error:', error));
        }

        // Update every 500ms
        setInterval(updateLocation, 500);
        updateLocation();
    </script>
</body>
</html>
)rawliteral";

void setup() {
    Serial.begin(115200);
    
  
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
 
    WiFi.softAP(ssid, password);
    Serial.println("\n=== ESP8266 Location Tracker ===");
    Serial.println("Access Point Started");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("Waiting for anchors to connect...");

   
    server.on("/", HTTP_GET, handleRoot);
    server.on("/position", HTTP_GET, handlePosition);
    server.on("/update", HTTP_POST, handleUpdate);
    server.on("/button_state", HTTP_GET, handleButtonState);
    
    server.begin();
    Serial.println("HTTP server started");
}

void handleRoot() {
    server.send(200, "text/html", INDEX_HTML);
}

void handlePosition() {
    StaticJsonDocument<200> doc;
    doc["x"] = pos_x;
    doc["y"] = pos_y;
    doc["rssi1"] = rssi_anchor1;
    doc["rssi2"] = rssi_anchor2;
    
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void handleUpdate() {
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, body);
        
        if (!error) {
            int anchor_id = doc["anchor_id"];
            float rssi = doc["rssi"];
            
            // Print RSSI updates
            Serial.print("Anchor ");
            Serial.print(anchor_id);
            Serial.print(" RSSI: ");
            Serial.print(rssi);
            Serial.println(" dBm");
            
            if (anchor_id == 1) {
                rssi_anchor1 = rssi;
            } else if (anchor_id == 2) {
                rssi_anchor2 = rssi;
            }
            
            // Calculate position only if we have data from both anchors
            if (rssi_anchor1 != 0 && rssi_anchor2 != 0) {
                calculatePosition();
            } else {
                Serial.println("Waiting for data from both anchors...");
            }
            
            server.send(200, "text/plain", "Updated");
        } else {
            Serial.println("Error parsing JSON");
            server.send(400, "text/plain", "Invalid JSON");
        }
    } else {
        server.send(400, "text/plain", "No data received");
    }
}

void handleButtonState() {
    String json = "{\"pressed\":" + String(buttonPressed ? "true" : "false") + "}";
    server.send(200, "application/json", json);
    buttonPressed = false;  // Reset after sending
}

void calculatePosition() {
    
    if (rssi_anchor1 == 0 || rssi_anchor2 == 0) {
        Serial.println("Waiting for valid RSSI values from both anchors...");
        return;
    }

    
    float d1 = rssiToDistance(rssi_anchor1);
    float d2 = rssiToDistance(rssi_anchor2);

    Serial.print("Distance from Anchor 1: "); Serial.print(d1); Serial.println(" cm");
    Serial.print("Distance from Anchor 2: "); Serial.print(d2); Serial.println(" cm");

    
    
    float total_distance = d1 + d2;
    if (total_distance > 0) {
        
        pos_x = ((ANCHOR1_X * d2) + (ANCHOR2_X * d1)) / total_distance;
        
        
        float dx1 = pos_x - ANCHOR1_X;
        float dy1 = sqrt(abs(d1 * d1 - dx1 * dx1));
        
        
        pos_y = ANCHOR1_Y + dy1;

        
        pos_x = constrain(pos_x, 0, 100);
        pos_y = constrain(pos_y, 0, 100);

        Serial.print("Calculated Position - X: ");
        Serial.print(pos_x);
        Serial.print(" cm, Y: ");
        Serial.print(pos_y);
        Serial.println(" cm");
    } else {
        Serial.println("Error: Invalid total distance");
    }
}

float rssiToDistance(float rssi) {
    // Apply Kalman filter to RSSI
    float filtered_rssi = kalmanFilter(rssi, (rssi == rssi_anchor1));

    if (filtered_rssi >= 0) return 200.0; 
    
    float ratio = (filtered_rssi - REF_RSSI) / (-10 * PATH_LOSS_EXPONENT);
    float distance = pow(10, ratio);
    
    
    distance = distance * 100;
    return constrain(distance, 10, 200);  
}

float kalmanFilter(float measurement, bool isAnchor1) {
    float& kalman_x = isAnchor1 ? kalman_x1 : kalman_x2;
    float& kalman_p = isAnchor1 ? kalman_p1 : kalman_p2;
    bool& kalman_init = isAnchor1 ? kalman_init1 : kalman_init2;
    
    // Initialize state if needed
    if (!kalman_init) {
        kalman_x = measurement;
        kalman_init = true;
        return measurement;
    }
    
    // Prediction
    float p_pred = kalman_p + KALMAN_Q;
    
    // Update
    float k = p_pred / (p_pred + KALMAN_R);
    kalman_x = kalman_x + k * (measurement - kalman_x);
    kalman_p = (1 - k) * p_pred;
    
    return kalman_x;
}

void checkButton() {
    bool currentButtonState = digitalRead(BUTTON_PIN);
    if (lastButtonState == HIGH && currentButtonState == LOW) {
        buttonPressed = true;
        Serial.println("Button pressed!");
    }
    
    lastButtonState = currentButtonState;
}

void loop() {
    server.handleClient();
    checkButton();
}
