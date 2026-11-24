#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <Update.h>

Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

const char* GITHUB_BIN = "https://github.com";
const char* GOOGLE_URL = "https://google.com";

bool isAP = false;
unsigned long lastSync = 0;

void sendLog(String m) {
  m.replace(" ", "%20");
  WiFiClientSecure c; c.setInsecure();
  HTTPClient h; h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  h.begin(c, String(GOOGLE_URL) + "?action=log&msg=" + m);
  h.GET(); h.end();
}

void runOTA() {
  WiFiClientSecure c; c.setInsecure(); c.setHandshakeTimeout(30);
  HTTPClient h; h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); h.setTimeout(30000);
  if (h.begin(c, GITHUB_BIN) && h.GET() == 200) {
    int len = h.getSize();
    if (Update.begin(len > 0 ? len : UPDATE_SIZE_UNKNOWN)) {
      size_t w = Update.writeStream(*h.getStreamPtr());
      if (Update.end(true) && Update.isFinished()) {
        h.end(); sendLog("Success_(" + String(w) + "B)"); delay(500); ESP.restart();
      } else sendLog("EndErr_" + String(Update.getError()));
    } else sendLog("SpaceErr");
  } else sendLog("HTTPErr");
  h.end(); delay(1000); ESP.restart();
}

void setupCloud(int ledPin) {
  pinMode(ledPin, OUTPUT); digitalWrite(ledPin, HIGH);
  prefs.begin("wifi", true);
  String s = prefs.getString("s", ""), p = prefs.getString("p", "");
  prefs.end();

  if (s != "") {
    WiFi.mode(WIFI_STA); WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.begin(s.c_str(), p.c_str());
    for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500); digitalWrite(ledPin, !digitalRead(ledPin));
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false); digitalWrite(ledPin, HIGH);
  } else {
    isAP = true;
    WiFi.mode(WIFI_AP_STA); WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.softAP("Esp32_setup_network", "12345678");
    dnsServer.start(53, "*", WiFi.softAPIP());
    digitalWrite(ledPin, LOW);
    server.on("/", []() {
      server.send(200, "text/html", "<meta charset='UTF-8'><body dir='rtl' style='background:#111;color:#fff;text-align:center;padding:30px'><h3>إعداد الواي فاي</h3><form action='/s' method='POST'><input name='s' placeholder='اسم الشبكة'><br><br><input name='p' type='password' placeholder='كلمة المرور'><br><br><button>حفظ</button></form></body>");
    });
    server.on("/s", HTTP_POST, []() {
      prefs.begin("wifi", false);
      prefs.putString("s", server.arg("s")); prefs.putString("p", server.arg("p"));
      prefs.end();
      server.send(200, "text/plain", "Saved! Restarting..."); delay(1000); ESP.restart();
    });
    server.onNotFound([]() { server.sendHeader("Location", "http://192.168.4"); server.send(302); });
    server.begin();
  }
}

void loopCloud(int ledPin) {
  if (isAP) { dnsServer.processNextRequest(); server.handleClient(); return; }
  if (millis() - lastSync < 2000 || WiFi.status() != WL_CONNECTED) return;
  lastSync = millis();

  WiFiClientSecure c; c.setInsecure();
  HTTPClient h; h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); h.setTimeout(10000);
  if (h.begin(c, String(GOOGLE_URL) + "?action=read") && h.GET() == 200) {
    String d = h.getString(); d.trim();
    int sep = d.indexOf('|');
    if (sep != -1) {
      String led = d.substring(0, sep), ota = d.substring(sep + 1);
      if (led == "1") digitalWrite(ledPin, LOW);
      else if (led == "0") digitalWrite(ledPin, HIGH);
      if (ota.length() > 0) {
        h.end();
        WiFiClientSecure c2; c2.setInsecure();
        HTTPClient h2; h2.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        h2.begin(c2, String(GOOGLE_URL) + "?action=clear_ota&msg=Downloading...");
        h2.GET(); h2.end();
        runOTA();
      }
    }
  }
  h.end();
}
