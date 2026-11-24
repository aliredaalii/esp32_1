#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <Update.h>

Preferences preferences;
WebServer server(80);
DNSServer dnsServer;

const char* GITHUB_BIN_URL = "https://github.com";
const char* GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbySJPgvgbPHD6JlYhaxWIHx752pHUr53FjoTrueu35NYJyCFQIScBUc16gBSrZ2FMiy/exec";

const char* AP_SSID = "Esp32_setup_network";
const char* AP_PASS = "12345678";

const int LED_PIN = 8;
String saved_ssid = "";
String saved_pass = "";
unsigned long lastCheck = 0;

void checkGitHubUpdate() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.update(client, GITHUB_BIN_URL);
}

void syncWithCloud() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  String url = String(GOOGLE_SCRIPT_URL) + "?action=read";
  if (http.begin(client, url)) {
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      payload.trim();
      
      int sep = payload.indexOf('|');
      if (sep != -1) {
        String ledVal = payload.substring(0, sep);
        String otaFlag = payload.substring(sep + 1);
        otaFlag.trim();

        // تشغيل أو إطفاء اللمبة مباشرة
        if (ledVal == "1") {
          digitalWrite(LED_PIN, LOW);  // تشغيل
        } else if (ledVal == "0") {
          digitalWrite(LED_PIN, HIGH); // إطفاء
        }

        // سحب التحديث من GitHub عند الضغط على زر التحديث في الموقع
        if (otaFlag.length() > 0) {
          http.end();
          WiFiClientSecure c2;
          c2.setInsecure();
          HTTPClient h2;
          h2.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
          h2.begin(c2, String(GOOGLE_SCRIPT_URL) + "?action=clear_ota");
          h2.GET();
          h2.end();
          
          checkGitHubUpdate();
          return;
        }
      }
    }
    http.end();
  }
}

void handleLocalPage() {
  int n = WiFi.scanNetworks();
  String html = "<!DOCTYPE html><html dir='rtl'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>إعداد ESP32</title><style>body{font-family:sans-serif;text-align:center;background:#121212;color:#fff;padding:15px;}";
  html += ".card{background:#1e1e1e;padding:18px;border-radius:12px;max-width:360px;margin:12px auto;}";
  html += "select,input,button{width:100%;padding:12px;margin:6px 0;border-radius:8px;border:none;font-size:15px;box-sizing:border-box;}";
  html += ".btn-wifi{background:#ff9100;color:#000;font-weight:bold;} #hidden_box{display:none;}</style>";
  html += "<script>function toggleHidden(val){document.getElementById('hidden_box').style.display=(val=='__HIDDEN__')?'block':'none';}</script></head><body>";

  html += "<div class='card'><h3>⚙️ إعداد شبكة الواي فاي</h3><form method='POST' action='/save'>";
  html += "<select name='ssid' onchange='toggleHidden(this.value)'>";
  html += "<option value='__HIDDEN__'>➕ إضافة شبكة مخفية (كتابة الاسم يدوياً)</option>";
  for (int i = 0; i < n; ++i) {
    html += "<option value='" + WiFi.SSID(i) + "'" + (i == 0 ? " selected" : "") + ">" + WiFi.SSID(i) + "</option>";
  }
  html += "</select>";
  html += "<div id='hidden_box'><input type='text' name='hidden_ssid' placeholder='اكتب اسم الشبكة المخفية بدقة'></div>";
  html += "<input type='password' name='pass' placeholder='كلمة مرور الواي فاي'>";
  html += "<button type='submit' class='btn-wifi'>حفظ والاتصال بالإنترنت</button></form></div>";
  html += "<script>toggleHidden(document.querySelector('select[name=ssid]').value);</script></body></html>";
  server.send(200, "text/html", html);
}

void handleSaveWiFi() {
  String s = server.arg("ssid");
  if (s == "__HIDDEN__") s = server.arg("hidden_ssid");
  s.trim();
  String p = server.arg("pass");

  preferences.begin("wifi-creds", false);
  preferences.putString("ssid", s);
  preferences.putString("pass", p);
  preferences.end();

  server.send(200, "text/html", "<h2 dir='rtl' style='text-align:center;margin-top:50px;'>✅ تم الحفظ، جاري الاتصال...</h2>");
  delay(1500);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  preferences.begin("wifi-creds", true);
  saved_ssid = preferences.getString("ssid", "");
  saved_pass = preferences.getString("pass", "");
  preferences.end();

  WiFi.mode(WIFI_AP_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(53, "*", WiFi.softAPIP());

  if (saved_ssid != "") {
    WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 24) {
      delay(500);
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      attempts++;
    }
    digitalWrite(LED_PIN, HIGH);
  }

  server.on("/", HTTP_GET, handleLocalPage);
  server.on("/save", HTTP_POST, handleSaveWiFi);
  server.onNotFound(handleLocalPage);
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (millis() - lastCheck > 2000) {
    lastCheck = millis();
    syncWithCloud();
  }
}
