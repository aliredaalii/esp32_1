#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <Update.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION 0
#endif

const int LED_PIN = 8;
const char* GOOGLE_URL = "https://script.google.com/macros/s/AKfycbw3mDoYiByHaNKgrp_-CYOZjoRViKxKJYo1-FRV7NhuFgfeg2Sj3S6iZbYeiq34L_1bvQ/exec";
const char* RELEASE = "https://github.com/aliredaalii/esp32_1/releases/download/latest/";
Preferences prefs;
WebServer server(80);
DNSServer dns;
String ssid, password, token;
bool portal = false;
unsigned long lastPoll = 0, lastReconnect = 0;

int lastGoogleHTTP = 0;
String lastGoogleReply = "";
unsigned long lastGoogleCheck = 0;

String request(const String& url, const String& body, bool diagnostic) {
  WiFiClientSecure client;
  client.setInsecure(); // Bench prototype only; replace with CA validation before deployment.
  HTTPClient http;
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    if (diagnostic) { lastGoogleHTTP = -1; lastGoogleReply = ""; lastGoogleCheck = millis(); }
    return "";
  }
  int status;
  if (body.length()) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    status = http.POST(body);
  } else status = http.GET();
  String result = status == 200 ? http.getString() : "";
  if (diagnostic) {
    lastGoogleHTTP = status;
    lastGoogleReply = result.substring(0, 160);
    if (token.length()) lastGoogleReply.replace(token, "[hidden]");
    lastGoogleCheck = millis();
  }
  http.end();
  result.trim();
  return result;
}

void startPortal() {
  portal = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Esp32_setup_network", "12345678");
  dns.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", R"HTML(<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><body style="font:18px Arial;text-align:center;background:#172033;color:white;padding:20px"><h2>ESP32 Setup</h2><form method="post" action="/save"><p><input name="s" placeholder="Wi-Fi name" required></p><p><input name="p" type="password" placeholder="Wi-Fi password"></p><p><input name="t" type="password" placeholder="Control access key" required></p><button>Save & Restart</button></form></body>)HTML");
  });
  server.on("/save", HTTP_POST, []() {
    String s = server.arg("s"), t = server.arg("t");
    t.trim();
    if (!s.length() || t.length() != 36) {
      server.send(400, "text/plain", "Invalid Wi-Fi name or access key");
      return;
    }
    for (unsigned int i=0; i<t.length(); i++) {
      char c=t[i];
      if (!isxdigit((unsigned char)c) && c!='-') {
        server.send(400,"text/plain","Invalid access key"); return;
      }
    }
    prefs.begin("wifi", false);
    prefs.putString("s", s);
    prefs.putString("p", server.arg("p"));
    prefs.putString("t", t);
    prefs.end();
    server.send(200, "text/plain", "Saved. Restarting...");
    delay(700);
    ESP.restart();
  });
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });
  server.begin();
}

void updateFirmware() {
  String version = request(String(RELEASE) + "version.txt", "", false);
  if (!version.length()) return;
  for (unsigned int i=0; i<version.length(); i++)
    if (!isDigit(version[i])) return;
  if (version.toInt() <= FIRMWARE_VERSION) return;
  WiFiClientSecure client;
  client.setInsecure(); // Prototype only.
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(30000);
  if (!http.begin(client, String(RELEASE) + "firmware.bin")) return;
  if (http.GET() == 200 && http.getSize() > 0) {
    size_t length = http.getSize();
    if (Update.begin(length)) {
      size_t written = Update.writeStream(*http.getStreamPtr());
      if (written == length && Update.end() && Update.isFinished()) {
        http.end();
        ESP.restart();
      } else {
        Update.abort();
      }
    }
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  prefs.begin("wifi", true);
  ssid = prefs.getString("s", "");
  password = prefs.getString("p", "");
  token = prefs.getString("t", "");
  prefs.end();
  if (!ssid.length() || !token.length()) { startPortal(); return; }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), password.c_str());
  for (int i=0; i<40 && WiFi.status()!=WL_CONNECTED; i++) delay(500);
  if (WiFi.status()!=WL_CONNECTED) startPortal();
}

void printStatus() {
  bool connected = WiFi.status() == WL_CONNECTED;
  Serial.println("\n--- ESP32 STATUS ---");
  Serial.printf("WiFi: %s (status %d)\n", connected ? "CONNECTED" : "DISCONNECTED", (int)WiFi.status());
  Serial.println("SSID: " + ssid);
  Serial.println("IP: " + (portal ? WiFi.softAPIP().toString() : WiFi.localIP().toString()));
  Serial.println("Setup portal: " + String(portal ? "ON" : "OFF"));
  if (connected) Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  Serial.println("Access key: " + String(token.length() ? "SAVED" : "MISSING"));
  Serial.printf("Firmware: %ld\n", (long)FIRMWARE_VERSION);
  Serial.printf("LED: %s\n", digitalRead(LED_PIN)==LOW ? "ON" : "OFF");
  Serial.printf("Free heap: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
  Serial.printf("Uptime: %lu seconds\n", millis()/1000);
  if (connected && token.length()) {
    request(String(GOOGLE_URL)+"?action=read&token="+token+
      "&version="+String(FIRMWARE_VERSION)+"&led="+(digitalRead(LED_PIN)==LOW?"1":"0"), "", true);
  }
  Serial.printf("Google HTTP: %d\n", lastGoogleHTTP);
  Serial.println("Google reply: " + lastGoogleReply);
  Serial.println("Google: " + String(connected && lastGoogleHTTP==200 &&
    (lastGoogleReply=="0|0" || lastGoogleReply=="0|1" || lastGoogleReply=="1|0" || lastGoogleReply=="1|1") ? "OK" : "NOT CONFIRMED"));
  Serial.println("--------------------");
}

void loop() {
  while (Serial.available()) {
    int ch = Serial.read();
    if (ch == 's' || ch == 'S') printStatus();
  }
  if (portal) { dns.processNextRequest(); server.handleClient(); delay(2); return; }
  if (WiFi.status()!=WL_CONNECTED) {
    if (millis()-lastReconnect>=15000) { lastReconnect=millis(); WiFi.reconnect(); }
    delay(10); return;
  }
  if (millis()-lastPoll<5000) { delay(10); return; }
  lastPoll=millis();
  String data=request(String(GOOGLE_URL)+"?action=read&token="+token+
    "&version="+String(FIRMWARE_VERSION)+"&led="+(digitalRead(LED_PIN)==LOW?"1":"0"), "", true);
  if (data=="0|0" || data=="0|1" || data=="1|0" || data=="1|1") {
    digitalWrite(LED_PIN, data[0]=='1'?LOW:HIGH);
    if (data[2]=='1') {
      request(GOOGLE_URL, "action=clear_ota&token="+token, false);
      updateFirmware();
    }
  }
}
