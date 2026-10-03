#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>

// CYD 2432S028 / ESP32-WROOM headless controller
// Safe, phone-first replacement for the screen-oriented UI.
// This build intentionally omits Wi-Fi deauth/jamming, credential capture,
// BadUSB payloads, and other disruptive/offensive functions.

static constexpr int PIN_SCK  = 18;
static constexpr int PIN_MISO = 19;
static constexpr int PIN_MOSI = 23;
static constexpr int NRF_CSN  = 27;
static constexpr int NRF_CE   = 22;
static constexpr int GPIO_IN_ONLY = 35;

static constexpr char AP_SSID[] = "CYD-CTRL";
static constexpr char AP_PASS[] = "cydcontrol";

WebServer server(80);
Preferences prefs;

String serialLine;
unsigned long bootMs = 0;

bool isSafeReadablePin(int pin) {
  return pin == 21 || pin == 22 || pin == 27 || pin == 35;
}

bool isSafeWritablePin(int pin) {
  return pin == 21 || pin == 22 || pin == 27;
}

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

String uptimeText() {
  unsigned long s = millis() / 1000UL;
  unsigned long d = s / 86400UL; s %= 86400UL;
  unsigned long h = s / 3600UL;  s %= 3600UL;
  unsigned long m = s / 60UL;    s %= 60UL;
  char buf[64];
  snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu", d, h, m, s);
  return String(buf);
}

String encryptionName(wifi_auth_mode_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "?";
  }
}

void printInfo(Stream &out) {
  out.println(F("=== CYD HEADLESS ==="));
  out.printf("Board       : ESP32-2432S028\n");
  out.printf("Chip        : %s rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  out.printf("Flash       : %u MB\n", ESP.getFlashChipSize() / (1024U * 1024U));
  out.printf("Heap free   : %u bytes\n", ESP.getFreeHeap());
  out.printf("Uptime      : %s\n", uptimeText().c_str());
  out.printf("AP          : %s\n", WiFi.softAPIP().toString().c_str());
  out.printf("STA         : %s\n", WiFi.localIP().toString().c_str());
  if (WiFi.status() == WL_CONNECTED) {
    out.printf("SSID        : %s\n", WiFi.SSID().c_str());
    out.printf("RSSI        : %d dBm\n", WiFi.RSSI());
  }
}

uint8_t nrfReadReg(uint8_t reg) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(NRF_CSN, LOW);
  SPI.transfer(0x00 | (reg & 0x1F));
  uint8_t v = SPI.transfer(0xFF);
  digitalWrite(NRF_CSN, HIGH);
  SPI.endTransaction();
  return v;
}

uint8_t nrfGetStatus() {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(NRF_CSN, LOW);
  uint8_t status = SPI.transfer(0xFF);
  digitalWrite(NRF_CSN, HIGH);
  SPI.endTransaction();
  return status;
}

bool nrfDetect(uint8_t &status, uint8_t &config) {
  pinMode(NRF_CSN, OUTPUT);
  pinMode(NRF_CE, OUTPUT);
  digitalWrite(NRF_CSN, HIGH);
  digitalWrite(NRF_CE, LOW);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, NRF_CSN);
  delay(3);

  // CONFIG is 0x00, STATUS is returned by an NOP (0xFF).
  config = nrfReadReg(0x00);
  status = nrfGetStatus();

  // A powered/responding nRF24 normally returns sane register values.
  // 0x00/0xFF is usually a wiring/power/CSN problem.
  return !(status == 0x00 || status == 0xFF) && !(config == 0x00 || config == 0xFF);
}

String nrfJson() {
  uint8_t status = 0, config = 0;
  bool ok = nrfDetect(status, config);
  String s = "{\"detected\":" + String(ok ? "true" : "false") +
             ",\"status\":" + String(status) +
             ",\"config\":" + String(config) +
             ",\"sck\":18,\"miso\":19,\"mosi\":23,\"csn\":27,\"ce\":22}";
  return s;
}

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD Headless</title>
<style>
:root{color-scheme:dark}body{font-family:system-ui;margin:0;background:#101217;color:#eee}main{max-width:760px;margin:auto;padding:16px}
.card{background:#191c23;border:1px solid #2a2f39;border-radius:14px;padding:14px;margin:12px 0}.row{display:flex;gap:8px;flex-wrap:wrap}button,input{font:inherit;border-radius:10px;border:1px solid #303640;padding:10px;background:#0f1217;color:#fff}button{cursor:pointer}input{flex:1;min-width:130px}pre{white-space:pre-wrap;background:#0a0c10;border-radius:10px;padding:12px;overflow:auto}.ok{color:#68e38b}.bad{color:#ff7777}.muted{color:#a9b0bd}
</style></head><body><main>
<h2>CYD Headless</h2><div class="muted">Phone control for ESP32-2432S028</div>
<div class="card"><b>Device</b><pre id="info">loading…</pre><button onclick="loadInfo()">Refresh</button></div>
<div class="card"><b>Wi-Fi scan</b><div class="row"><button onclick="scan()">Scan</button></div><pre id="scan">—</pre></div>
<div class="card"><b>nRF24 check</b><div class="row"><button onclick="nrf()">Detect module</button></div><pre id="nrf">—</pre></div>
<div class="card"><b>GPIO</b><div class="row"><input id="pin" placeholder="21 / 22 / 27 / 35"><input id="val" placeholder="0 or 1"><button onclick="gpioWrite()">Write</button><button onclick="gpioRead()">Read</button></div><pre id="gpio">—</pre></div>
<div class="card"><b>Wi-Fi STA</b><div class="row"><input id="ssid" placeholder="SSID"><input id="pass" placeholder="password" type="password"><button onclick="connectWifi()">Connect</button></div><pre id="wout">—</pre></div>
<div class="card"><b>System</b><div class="row"><button onclick="reboot()">Reboot</button><button onclick="factoryReset()">Reset saved Wi-Fi</button></div></div>
<script>
async function j(u,o){let r=await fetch(u,o);return await r.text()}
async function loadInfo(){document.querySelector('#info').textContent=await j('/api/info')}
async function scan(){document.querySelector('#scan').textContent=await j('/api/scan')}
async function nrf(){document.querySelector('#nrf').textContent=await j('/api/nrf24')}
async function gpioRead(){let p=document.querySelector('#pin').value;document.querySelector('#gpio').textContent=await j('/api/gpio/read?pin='+encodeURIComponent(p))}
async function gpioWrite(){let p=document.querySelector('#pin').value,v=document.querySelector('#val').value;document.querySelector('#gpio').textContent=await j('/api/gpio/write?pin='+encodeURIComponent(p)+'&value='+encodeURIComponent(v))}
async function connectWifi(){let s=document.querySelector('#ssid').value,p=document.querySelector('#pass').value;document.querySelector('#wout').textContent=await j('/api/wifi/connect?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p))}
async function reboot(){await fetch('/api/reboot',{method:'POST'});document.body.innerHTML='<main><h2>Rebooting…</h2></main>'}
async function factoryReset(){document.querySelector('#wout').textContent=await j('/api/factory-reset',{method:'POST'})}
loadInfo();
</script></main></body></html>
)HTML";

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleInfo() {
  String s = "{\"board\":\"ESP32-2432S028\",\"chip\":\"" + jsonEscape(ESP.getChipModel()) +
             "\",\"revision\":" + String(ESP.getChipRevision()) +
             ",\"flash_mb\":" + String(ESP.getFlashChipSize() / (1024U * 1024U)) +
             ",\"heap\":" + String(ESP.getFreeHeap()) +
             ",\"uptime\":\"" + jsonEscape(uptimeText()) +
             "\",\"ap_ip\":\"" + WiFi.softAPIP().toString() +
             "\",\"sta_ip\":\"" + WiFi.localIP().toString() +
             "\",\"sta_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + "}";
  server.send(200, "application/json", s);
}

void handleScan() {
  int n = WiFi.scanNetworks(false, true);
  String s = "";
  if (n < 0) {
    server.send(500, "text/plain", "scan failed");
    return;
  }
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) ssid = "<hidden>";
    s += String(i) + " | " + ssid + " | " + String(WiFi.RSSI(i)) + " dBm | ch " + String(WiFi.channel(i)) + " | " + encryptionName(WiFi.encryptionType(i)) + "\n";
  }
  WiFi.scanDelete();
  server.send(200, "text/plain; charset=utf-8", s.length() ? s : "no networks found");
}

void handleNrf() {
  server.send(200, "application/json", nrfJson());
}

int argPin() {
  if (!server.hasArg("pin")) return -1;
  return server.arg("pin").toInt();
}

void handleGpioRead() {
  int p = argPin();
  if (!isSafeReadablePin(p)) {
    server.send(400, "text/plain", "allowed read pins: 21,22,27,35");
    return;
  }
  pinMode(p, INPUT);
  server.send(200, "text/plain", "GPIO " + String(p) + " = " + String(digitalRead(p)));
}

void handleGpioWrite() {
  int p = argPin();
  if (!isSafeWritablePin(p)) {
    server.send(400, "text/plain", "allowed write pins: 21,22,27");
    return;
  }
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "missing value");
    return;
  }
  int v = server.arg("value").toInt() ? HIGH : LOW;
  pinMode(p, OUTPUT);
  digitalWrite(p, v);
  server.send(200, "text/plain", "GPIO " + String(p) + " <- " + String(v == HIGH ? 1 : 0));
}

void handleWifiConnect() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "missing ssid");
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) delay(100);
  if (WiFi.status() == WL_CONNECTED) {
    server.send(200, "text/plain", "connected: " + WiFi.localIP().toString());
  } else {
    server.send(200, "text/plain", "connection failed / timed out; AP stays at " + WiFi.softAPIP().toString());
  }
}

void handleFactoryReset() {
  prefs.clear();
  server.send(200, "text/plain", "saved Wi-Fi credentials cleared");
}

void handleReboot() {
  server.send(200, "text/plain", "rebooting");
  delay(150);
  ESP.restart();
}

void serialHelp(Stream &out) {
  out.println(F("Commands:"));
  out.println(F("  help"));
  out.println(F("  info"));
  out.println(F("  scan"));
  out.println(F("  nrf24"));
  out.println(F("  gpio read <21|22|27|35>"));
  out.println(F("  gpio write <21|22|27> <0|1>"));
  out.println(F("  wifi connect <ssid> <password>"));
  out.println(F("  wifi status"));
  out.println(F("  uptime"));
  out.println(F("  free"));
  out.println(F("  reboot"));
  out.println(F("  factory_reset"));
}

void handleSerialCommand(String cmd) {
  cmd.trim();
  if (!cmd.length()) return;

  if (cmd == "help") { serialHelp(Serial); return; }
  if (cmd == "info") { printInfo(Serial); return; }
  if (cmd == "scan") {
    int n = WiFi.scanNetworks();
    Serial.printf("Found %d networks\n", n);
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      if (ssid.isEmpty()) ssid = "<hidden>";
      Serial.printf("%02d | %s | %d dBm | ch %d | %s\n", i, ssid.c_str(), WiFi.RSSI(i), WiFi.channel(i), encryptionName(WiFi.encryptionType(i)).c_str());
    }
    WiFi.scanDelete();
    return;
  }
  if (cmd == "nrf24") {
    uint8_t st = 0, cfg = 0;
    bool ok = nrfDetect(st, cfg);
    Serial.printf("nRF24: %s | STATUS=0x%02X CONFIG=0x%02X | SCK=18 MISO=19 MOSI=23 CSN=27 CE=22\n", ok ? "DETECTED?" : "NO RESPONSE", st, cfg);
    return;
  }
  if (cmd == "uptime") { Serial.println(uptimeText()); return; }
  if (cmd == "free") { Serial.printf("free heap: %u\n", ESP.getFreeHeap()); return; }
  if (cmd == "reboot") { Serial.println("rebooting..."); delay(100); ESP.restart(); return; }
  if (cmd == "factory_reset") { prefs.clear(); Serial.println("saved Wi-Fi cleared"); return; }
  if (cmd == "wifi status") {
    Serial.printf("Wi-Fi: %s\n", WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
    Serial.printf("STA IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("AP IP : %s\n", WiFi.softAPIP().toString().c_str());
    return;
  }

  if (cmd.startsWith("wifi connect ")) {
    String rest = cmd.substring(13);
    int sp = rest.indexOf(' ');
    if (sp <= 0) { Serial.println("usage: wifi connect <ssid> <password>"); return; }
    String ssid = rest.substring(0, sp);
    String pass = rest.substring(sp + 1);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("connecting to %s...\n", ssid.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) delay(100);
    if (WiFi.status() == WL_CONNECTED) Serial.printf("connected: %s\n", WiFi.localIP().toString().c_str());
    else Serial.println("connection failed / timed out");
    return;
  }

  if (cmd.startsWith("gpio read ")) {
    int p = cmd.substring(10).toInt();
    if (!isSafeReadablePin(p)) { Serial.println("allowed read pins: 21,22,27,35"); return; }
    pinMode(p, INPUT);
    Serial.printf("GPIO %d = %d\n", p, digitalRead(p));
    return;
  }

  if (cmd.startsWith("gpio write ")) {
    String rest = cmd.substring(11);
    int sp = rest.indexOf(' ');
    if (sp <= 0) { Serial.println("usage: gpio write <21|22|27> <0|1>"); return; }
    int p = rest.substring(0, sp).toInt();
    int v = rest.substring(sp + 1).toInt() ? HIGH : LOW;
    if (!isSafeWritablePin(p)) { Serial.println("allowed write pins: 21,22,27"); return; }
    pinMode(p, OUTPUT);
    digitalWrite(p, v);
    Serial.printf("GPIO %d <- %d\n", p, v == HIGH ? 1 : 0);
    return;
  }

  Serial.println(F("unknown command; use help"));
}

void setupWeb() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/nrf24", HTTP_GET, handleNrf);
  server.on("/api/gpio/read", HTTP_GET, handleGpioRead);
  server.on("/api/gpio/write", HTTP_GET, handleGpioWrite);
  server.on("/api/wifi/connect", HTTP_GET, handleWifiConnect);
  server.on("/api/factory-reset", HTTP_POST, handleFactoryReset);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.begin();
}

void setup() {
  bootMs = millis();
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("CYD Headless v1"));

  prefs.begin("cyd", false);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS, 1, false, 4);
  Serial.printf("AP SSID: %s\n", AP_SSID);
  Serial.printf("AP IP  : %s\n", WiFi.softAPIP().toString().c_str());

  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  if (ssid.length()) {
    Serial.printf("saved Wi-Fi: %s\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
  }

  setupWeb();
  Serial.println(F("Web UI: http://192.168.4.1"));
  Serial.println(F("Type 'help' for serial commands."));
}

void loop() {
  server.handleClient();

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLine.length()) {
        handleSerialCommand(serialLine);
        serialLine = "";
      }
    } else if (serialLine.length() < 180) {
      serialLine += c;
    }
  }

  delay(2);
}
