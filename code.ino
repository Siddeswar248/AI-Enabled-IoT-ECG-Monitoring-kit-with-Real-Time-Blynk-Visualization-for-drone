#define BLYNK_PRINT Serial

#define BLYNK_TEMPLATE_ID   "TMPLxxxxxx"
#define BLYNK_TEMPLATE_NAME "ECG Monitor"
#define BLYNK_AUTH_TOKEN    "YOUR_DEVICE_AUTH_TOKEN"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ================= WIFI =================
char ssid[] = "YOUR_WIFI_NAME";
char pass[] = "YOUR_WIFI_PASSWORD";

// ================= GROQ =================
String GROQ_API_KEY = "gsk_YOUR_GROQ_KEY";

// ================= PINS =================
#define ECG_PIN     34
#define LO_PLUS     32
#define LO_MINUS    33
#define LED_ASPIRIN 26
#define LED_NITRO   27

BlynkTimer timer;

// ================ VARIABLES ===============
String ecgBuffer = "";
int bpm = 0;
unsigned long lastPeakTime = 0;
bool peakDetected = false;
String rhythm = "WAITING";

bool aspirinState = false;
bool nitroState = false;

int lastECG = 0;

// ============== LEAD CHECK ===============
bool leadsConnected() {
  return digitalRead(LO_PLUS) == LOW &&
         digitalRead(LO_MINUS) == LOW;
}

// ============== ECG SAMPLING (FAST) ======
void sampleECG() {
  if (!leadsConnected()) {
    lastECG = 0;
    bpm = 0;
    rhythm = "NO LEADS";
    return;
  }

  lastECG = analogRead(ECG_PIN);

  ecgBuffer += String(lastECG) + ",";
  if (ecgBuffer.length() > 800)
    ecgBuffer.remove(0, 300);

  detectBPM(lastECG);
}

// ============== SERIAL PLOTTER ===========
void plotECG() {
  Serial.println(lastECG);   // numbers only
}

// ============== BPM ======================
void detectBPM(int ecg) {
  int threshold = 2200;

  if (ecg > threshold && !peakDetected) {
    peakDetected = true;
    unsigned long now = millis();
    if (lastPeakTime > 0)
      bpm = 60000 / (now - lastPeakTime);
    lastPeakTime = now;
  }

  if (ecg < threshold)
    peakDetected = false;
}

// ============== SEND TO BLYNK ============
void sendToBlynk() {
  if (!leadsConnected()) {
    Blynk.virtualWrite(V4, "LEADS OFF");
    return;
  }

  Blynk.virtualWrite(V0, lastECG);   // ECG graph
  Blynk.virtualWrite(V2, bpm);       // BPM
  Blynk.virtualWrite(V4, "LEADS OK");
}

// ============== AI ANALYSIS ==============
void analyzeECG() {
  if (!leadsConnected()) return;
  if (bpm < 40 || bpm > 180) return;

  Serial.println("[AI] Sending ECG to Groq...");

  HTTPClient http;
  http.begin("https://api.groq.com/openai/v1/chat/completions");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + GROQ_API_KEY);

  StaticJsonDocument<2048> doc;
  doc["model"] = "openai/gpt-oss-20b";

  JsonArray messages = doc.createNestedArray("messages");

  messages.createNestedObject()["role"] = "system";
  messages[0]["content"] =
    "Reply ONLY with NORMAL or IRREGULAR ECG rhythm.";

  messages.createNestedObject()["role"] = "user";
  messages[1]["content"] =
    "ECG samples: " + ecgBuffer +
    " BPM: " + String(bpm);

  String payload;
  serializeJson(doc, payload);

  if (http.POST(payload) > 0) {
    String res = http.getString();
    rhythm = (res.indexOf("NORMAL") != -1) ? "NORMAL" : "IRREGULAR";
    Blynk.virtualWrite(V3, rhythm);
    Serial.print("[AI] Rhythm: ");
    Serial.println(rhythm);
  } else {
    Serial.println("[AI] Error");
  }

  http.end();
}

// ============== SERIAL MONITOR ===========
void printStatus() {
  Serial.println("\n====== ECG SYSTEM ======");
  Serial.print("WiFi: ");
  Serial.println(WiFi.isConnected() ? "CONNECTED" : "DISCONNECTED");

  Serial.print("Blynk: ");
  Serial.println(Blynk.connected() ? "CONNECTED" : "DISCONNECTED");

  Serial.print("ECG Value: ");
  Serial.println(lastECG);

  Serial.print("BPM: ");
  Serial.println(bpm);

  Serial.print("Rhythm: ");
  Serial.println(rhythm);

  Serial.print("Leads: ");
  Serial.println(leadsConnected() ? "OK" : "OFF");

  Serial.print("Aspirin LED: ");
  Serial.println(aspirinState ? "ON" : "OFF");

  Serial.print("Nitro LED: ");
  Serial.println(nitroState ? "ON" : "OFF");

  Serial.println("========================");
}

// ============== BLYNK BUTTONS ============
BLYNK_WRITE(V5) {
  aspirinState = param.asInt();
  digitalWrite(LED_ASPIRIN, aspirinState);
}

BLYNK_WRITE(V1) {
  nitroState = param.asInt();
  digitalWrite(LED_NITRO, nitroState);
}

// ============== SETUP ====================
void setup() {
  Serial.begin(115200);

  pinMode(LED_ASPIRIN, OUTPUT);
  pinMode(LED_NITRO, OUTPUT);
  pinMode(LO_PLUS, INPUT);
  pinMode(LO_MINUS, INPUT);

  Serial.println("Connecting to WiFi...");
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  timer.setInterval(5L, sampleECG);       // fast ECG sampling
  timer.setInterval(10L, plotECG);        // plot speed
  timer.setInterval(200L, sendToBlynk);   // visible graph
  timer.setInterval(15000L, analyzeECG);  // AI
  timer.setInterval(3000L, printStatus);  // readable serial
}

// ============== LOOP =====================
void loop() {
  Blynk.run();
  timer.run();
}
