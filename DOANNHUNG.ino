#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define MQ3_PIN 34
#define BUTTON1_PIN 18
#define BUTTON2_PIN 19
#define RESET_PIN 25

#define VCC 3.3
#define RL_VALUE 1.0
#define RO_CLEAN_AIR 10.0
#define ADC_RESOLUTION 4095.0

const char* ssid = "Kvv";
const char* password = "66668888";
const char* mqtt_server = "172.20.10.5";
const int mqtt_port = 1883;
const char* thingspeak_api_key = "7MJU8KE3CS1A6Y36";
const char* thingspeak_server = "http://api.thingspeak.com/update";

WiFiClient espClient;
PubSubClient client(espClient);

bool mqttEnabled = false;
bool button1Pressed = false;
bool button2Pressed = false;
bool resetPressed = false;
unsigned long lastMqttAttempt = 0;

void setup() {
    pinMode(BUTTON1_PIN, INPUT_PULLUP);
    pinMode(BUTTON2_PIN, INPUT_PULLUP);
    pinMode(RESET_PIN, INPUT_PULLUP);

    Serial.begin(115200);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;);
    }
    display.clearDisplay();
    display.display();

    WiFi.begin(ssid, password);
    Serial.print("🔄 Đang kết nối WiFi...");
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 15) {
        delay(1000);
        Serial.print(".");
        timeout++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ Đã kết nối WiFi");
        client.setServer(mqtt_server, mqtt_port);
        client.setCallback(mqttCallback);
        reconnectMQTT();
    } else {
        Serial.println("\n❌ Không kết nối được WiFi, hoạt động offline!");
    }
}

void loop() {
    if (mqttEnabled && !client.connected() && millis() - lastMqttAttempt > 5000) {
        reconnectMQTT();
    }
    if (mqttEnabled) {
        client.loop();
    }

    int button1State = digitalRead(BUTTON1_PIN);
    int button2State = digitalRead(BUTTON2_PIN);
    int resetState = digitalRead(RESET_PIN);

    if (button1State == LOW && !button1Pressed) {
        button1Pressed = true;
        displayMessage("XE MAY");

        float alcoholPPM = readAlcoholConcentration();
        if (alcoholPPM == -1) {
            displayMessage("Lỗi cảm biến!");
            Serial.println("❌ Lỗi: Cảm biến không đọc được dữ liệu!");
        } else {
            displayAlcoholConcentration(alcoholPPM, "bike");
            sendData(alcoholPPM, "bike");
        }
    }

    if (button2State == LOW && !button2Pressed) {
        button2Pressed = true;
        displayMessage("O TO");

        float alcoholPPM = readAlcoholConcentration();
        if (alcoholPPM == -1) {
            displayMessage("Lỗi cảm biến!");
            Serial.println("❌ Lỗi: Cảm biến không đọc được dữ liệu!");
        } else {
            displayAlcoholConcentration(alcoholPPM, "car");
            sendData(alcoholPPM, "car");
        }
    }

    if (resetState == LOW && !resetPressed) {
        resetPressed = true;
        displayMessage("RESET");
    }

    if (button1State == HIGH) button1Pressed = false;
    if (button2State == HIGH) button2Pressed = false;
    if (resetState == HIGH) resetPressed = false;
}

void reconnectMQTT() {
    Serial.print("🔄 Đang kết nối MQTT...");
    lastMqttAttempt = millis();
    if (client.connect("ESP32_Client")) {
        Serial.println("✅ Đã kết nối MQTT");
        mqttEnabled = true;
    } else {
        Serial.print("❌ Lỗi MQTT, mã lỗi: ");
        Serial.println(client.state());
        mqttEnabled = false;
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.print("📩 MQTT nhận dữ liệu từ topic: ");
    Serial.println(topic);

    String message = "";
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.println("Nội dung: " + message);
}

void displayMessage(String message) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(message);
    display.display();
    delay(3000);
}

float readAlcoholConcentration() {
    int sensorValue = analogRead(MQ3_PIN);
    if (sensorValue < 0 || sensorValue > 4095) {
        return -1;
    }
    float voltage = (sensorValue / ADC_RESOLUTION) * VCC;
    float Rs = (VCC - voltage) / voltage * RL_VALUE;
    float ratio = Rs / RO_CLEAN_AIR;
    float ppm = pow(10, (-0.66 + (-3.13 * log10(ratio))));

    return (ppm < 200) ? 0 : ppm;
}

void displayAlcoholConcentration(float alcoholPPM, String vehicleType) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(vehicleType == "bike" ? "XE MAY: " : "O TO: ");
    display.print(alcoholPPM);
    display.println(" PPM");
    display.print("Muc phat: ");
    display.println(getFine(alcoholPPM, vehicleType));
    display.display();

    Serial.print(vehicleType == "bike" ? "XE MAY: " : "O TO: ");
    Serial.print(alcoholPPM);
    Serial.println(" PPM");
    Serial.print("Muc phat: ");
    Serial.println(getFine(alcoholPPM, vehicleType));
}

void sendData(float alcoholPPM, String vehicleType) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = String(thingspeak_server) + "?api_key=" + thingspeak_api_key + "&field1=" + String(alcoholPPM);
        http.begin(url);
        int httpCode = http.GET();
        http.end();
        Serial.println(httpCode == HTTP_CODE_OK ? "✅ Đã gửi lên Thingspeak" : "❌ Lỗi gửi dữ liệu");
    }
    if (mqttEnabled) {
        String topic = vehicleType == "bike" ? "sensor/bike" : "sensor/car";
        String payload = String(alcoholPPM);
        client.publish(topic.c_str(), payload.c_str());
        Serial.println("✅ Đã gửi lên MQTT");
    } else {
        Serial.println("❌ Không gửi MQTT do mất kết nối");
    }
}

String getFine(float alcoholPPM, String vehicleType) {
    if (alcoholPPM == 0) return "Khong phat";
    if (vehicleType == "bike") {
        if (alcoholPPM <= 0.25) return "2-3 trieu VND";
        else if (alcoholPPM <= 0.4) return "6-8 trieu VND";
        else return "8-10 trieu VND";
    } else {
        if (alcoholPPM <= 0.25) return "6-8 trieu VND";
        else if (alcoholPPM <= 0.4) return "18-20 trieu VND";
        else return "20-30 trieu VND";
    }
}
