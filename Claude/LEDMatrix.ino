/*
 * ESP32-S3 HUB75 LED Matrix Controller
 * 硬體：ESP32-S3-WROOM-1 + 74HCT245 + HUB75 64x32
 * 框架：Arduino IDE (ESP32 Arduino Core >= 2.0.5)
 *
 * 依賴函式庫：
 *   - ESP32-HUB75-MatrixPanel-I2S-DMA (by mrfaptastic)
 *   - ESPAsyncWebServer (by me-no-dev)
 *   - AsyncTCP (by me-no-dev)
 *   - AnimatedGIF (by Larry Bank)
 *   - ArduinoJson (by Benoit Blanchon)
 *
 * 分區表（partitions.csv）需自定義，範例：
 *   # Name,   Type, SubType, Offset,  Size,  Flags
 *   nvs,      data, nvs,     0x9000,  0x5000,
 *   otadata,  data, ota,     0xe000,  0x2000,
 *   app0,     app,  ota_0,   0x10000, 0x300000,
 *   fatfs,    data, fat,     0x310000,0xCF0000,
 *
 * USB MSC 運作方式：
 *   接上電腦 → 電腦看到「LEDMATRIX」隨身碟
 *   直接用檔案總管拖放 index.html / GIF 等檔案
 *   在電腦安全退出後 → ESP32 自動重新掛載 FATFS 繼續執行
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include <FFat.h>
#include <USB.h>
#include <USBMSC.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// ============================================================
// IO2 LED 腳位
// ============================================================
#define LED_IO2   2   // 白色 LED，低電位亮

// ============================================================
// 系統模式枚舉（用於統一管理燈號與面板顯示）
// ============================================================
enum SysMode {
  SYS_BOOTING,       // 開機初始化
  SYS_USB_MSC,       // USB 隨身碟存取中
  SYS_AP_WAIT,       // AP 熱點等待設定
  SYS_WIFI_CONN,     // Wi-Fi 連線中
  SYS_WIFI_OK,       // Wi-Fi 已連線（NTP 同步中或完成）
  SYS_OTA,           // OTA 更新中
  SYS_RUNNING,       // 正常執行（顯示時鐘/跑馬燈/GIF）
};
volatile SysMode sysMode = SYS_BOOTING;

// ============================================================
// HUB75 腳位（依您的電路圖確認）
// ============================================================
#define R1_PIN   4
#define G1_PIN   5
#define B1_PIN   6
#define R2_PIN   7
#define G2_PIN   15
#define B2_PIN   16
#define A_PIN    17
#define B_PIN    18
#define C_PIN    8
#define D_PIN    3
#define E_PIN    46
#define LAT_PIN  47
#define OE_PIN   48
#define CLK_PIN  45

// ============================================================
// 面板規格
// ============================================================
#define PANEL_WIDTH   64
#define PANEL_HEIGHT  32
#define PANEL_CHAIN   1

// ============================================================
// 系統設定
// ============================================================
#define HOSTNAME        "ledmatrix"
#define AP_SSID         "LEDMatrix-Setup"
#define AP_PASSWORD     "12345678"
#define NTP_SERVER      "pool.ntp.org"
#define NTP_GMT_OFFSET  28800
#define FATFS_MOUNT     "/fatfs"
#define FATFS_LABEL     "LEDMATRIX"

// ============================================================
// USB MSC
// ============================================================
USBMSC msc;
volatile bool usbMounted  = false;
volatile bool fatfsMounted = false;

static const esp_partition_t* getFatPartition() {
  return esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "fatfs");
}

static int32_t mscRead(uint32_t lba, uint32_t offset,
                        void *buf, uint32_t bufsize) {
  const esp_partition_t *p = getFatPartition();
  if (!p) return -1;
  return (esp_partition_read(p, (uint64_t)lba * 512 + offset,
                              buf, bufsize) == ESP_OK) ? bufsize : -1;
}

static int32_t mscWrite(uint32_t lba, uint32_t offset,
                         const void *buf, uint32_t bufsize) {
  const esp_partition_t *p = getFatPartition();
  if (!p) return -1;
  uint32_t addr = (uint64_t)lba * 512 + offset;
  // Flash 寫入前需先按扇區 erase（4096 byte 對齊）
  uint32_t eraseStart = addr & ~(4095u);
  uint32_t eraseEnd   = (addr + bufsize + 4095) & ~(4095u);
  if (esp_partition_erase_range(p, eraseStart,
        eraseEnd - eraseStart) != ESP_OK) return -1;
  return (esp_partition_write(p, addr, buf, bufsize)
          == ESP_OK) ? bufsize : -1;
}

static void mscStartStop(uint8_t power_condition,
                          bool start, bool load_eject) {
  if (start) {
    if (fatfsMounted) { FFat.end(); fatfsMounted = false; }
    usbMounted = true;
    sysMode = SYS_USB_MSC;
    Serial.println("[MSC] 電腦已掛載 — FATFS 暫停");
  } else {
    usbMounted = false;
    if (FFat.begin(true, FATFS_MOUNT, 10, FATFS_LABEL)) {
      fatfsMounted = true;
      Serial.println("[MSC] 電腦已退出 — FATFS 重新掛載");
    }
    // 恢復正常執行模式
    sysMode = SYS_RUNNING;
  }
}

void setupUSBMSC() {
  const esp_partition_t *p = getFatPartition();
  if (!p) {
    Serial.println("[MSC] 錯誤：找不到 fatfs 分區，請確認 partitions.csv");
    return;
  }
  uint32_t sectors = p->size / 512;
  msc.vendorID("ESP32-S3");
  msc.productID("LED-Matrix");
  msc.productRevision("1.0");
  msc.onRead(mscRead);
  msc.onWrite(mscWrite);
  msc.onStartStop(mscStartStop);
  msc.mediaPresent(true);
  msc.begin(sectors, 512);
  USB.begin();
  Serial.printf("[MSC] USB MSC 就緒，分區大小: %lu KB\n", p->size / 1024);
}

// ============================================================
// 全域物件
// ============================================================
MatrixPanel_I2S_DMA *matrix = nullptr;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences prefs;

// ============================================================
// 系統狀態
// ============================================================
struct SystemState {
  String  ssid, password;
  bool    wifiConnected = false;
  bool    apMode        = false;
  uint8_t brightness    = 50;
  uint8_t displayMode   = 0;   // 0=時鐘 1=跑馬燈 2=GIF
  String  scrollText    = "Hello!";
  int     scrollSpeed   = 40;
  bool    ntpSynced     = false;
  int     scrollX       = PANEL_WIDTH;
} state;

TaskHandle_t renderTaskHandle = NULL;

// ============================================================
// 工具
// ============================================================
uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
  return matrix->color565(r, g, b);
}

// ============================================================
// Wi-Fi
// ============================================================
bool connectWiFi(const String &ssid, const String &pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("連線至 %s", ssid.c_str());
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n已連線 IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("\n連線失敗");
  return false;
}

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  state.apMode = true;
  Serial.printf("AP: %s  IP: %s\n", AP_SSID,
    WiFi.softAPIP().toString().c_str());
}

// ============================================================
// NTP
// ============================================================
void syncNTP() {
  configTime(NTP_GMT_OFFSET, 0, NTP_SERVER);
  struct tm t;
  if (getLocalTime(&t, 5000)) {
    state.ntpSynced = true;
    Serial.println("NTP 同步成功");
  }
}

// ============================================================
// 顯示
// ============================================================
void drawClock() {
  struct tm t;
  if (!getLocalTime(&t)) return;
  matrix->fillScreen(0);

  char buf[16];
  strftime(buf, sizeof(buf), "%H:%M:%S", &t);
  matrix->setTextSize(1);
  matrix->setTextColor(color565(0, 200, 255));
  matrix->setCursor(2, 2); matrix->print(buf);

  strftime(buf, sizeof(buf), "%m/%d", &t);
  matrix->setTextColor(color565(150, 150, 150));
  matrix->setCursor(2, 14); matrix->print(buf);

  const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  matrix->setTextColor(color565(255, 200, 0));
  matrix->setCursor(36, 14); matrix->print(days[t.tm_wday]);

  // 狀態指示點（右上角）
  // 橘色=USB存取中 / 綠色=Wi-Fi連線 / 紅色=無Wi-Fi
  uint16_t dot = usbMounted          ? color565(255,100,0) :
                 state.wifiConnected ? color565(0,255,0)   :
                                       color565(255,0,0);
  matrix->drawPixel(62, 1, dot);
}

void drawScrollText() {
  matrix->fillScreen(0);
  matrix->setTextSize(1);
  matrix->setTextColor(color565(255, 100, 0));
  matrix->setCursor(state.scrollX, 12);
  matrix->print(state.scrollText);
  state.scrollX--;
  if (state.scrollX < -(int)(state.scrollText.length() * 6))
    state.scrollX = PANEL_WIDTH;
}

// ============================================================
// 面板狀態畫面
// 用於非正常顯示模式時，全螢幕顯示系統狀態說明
// ============================================================

// 狀態設定：像素顏色 / 第一行 / 第二行文字
struct StatusScreen {
  uint8_t  dotR, dotG, dotB;   // 右上角像素顏色
  const char *line1;            // 第一行（y=4）
  const char *line2;            // 第二行（y=14）
  const char *line3;            // 第三行（y=24，可為 nullptr）
};

static const StatusScreen STATUS_TABLE[] = {
  // SYS_BOOTING
  { 255, 255,   0, "BOOT",   "Init...", nullptr       },
  // SYS_USB_MSC
  { 255, 100,   0, "USB",    "MSC",    "Eject to exit"},
  // SYS_AP_WAIT
  {   0,   0, 255, "AP Mode","Setup",  AP_SSID        },
  // SYS_WIFI_CONN
  { 255, 200,   0, "WiFi",   "Conn...", nullptr       },
  // SYS_WIFI_OK
  {   0, 255,   0, "WiFi OK","NTP OK",  nullptr       },
  // SYS_OTA
  {   0, 100, 255, "OTA",    "Update", "Please wait" },
  // SYS_RUNNING  （正常執行，不顯示狀態畫面）
  {   0, 255,   0, nullptr,  nullptr,  nullptr        },
};

void drawStatusScreen(SysMode mode) {
  static bool blink = false;
  blink = !blink;

  const StatusScreen &s = STATUS_TABLE[mode];
  matrix->fillScreen(0);
  matrix->setTextSize(1);

  // 右上角狀態像素（SYS_USB_MSC / SYS_AP_WAIT 時閃爍）
  bool shouldBlink = (mode == SYS_USB_MSC || mode == SYS_AP_WAIT
                   || mode == SYS_WIFI_CONN || mode == SYS_OTA);
  if (!shouldBlink || blink) {
    matrix->drawPixel(63, 0, color565(s.dotR, s.dotG, s.dotB));
  }

  // 文字顏色與像素同色
  uint16_t textColor = color565(s.dotR, s.dotG, s.dotB);

  if (s.line1) {
    matrix->setTextColor(textColor);
    matrix->setCursor(2, 4);
    matrix->print(s.line1);
  }
  if (s.line2) {
    // 第二行用稍暗色區隔
    matrix->setTextColor(color565(s.dotR/2, s.dotG/2, s.dotB/2));
    matrix->setCursor(2, 14);
    matrix->print(s.line2);
  }
  if (s.line3) {
    // 第三行：AP SSID / 提示文字，用最暗色，視情況跑馬燈
    static int apScroll = PANEL_WIDTH;
    matrix->setTextColor(color565(s.dotR/3, s.dotG/3, s.dotB/3));
    matrix->setCursor(apScroll, 24);
    matrix->print(s.line3);
    apScroll--;
    int textW = strlen(s.line3) * 6;
    if (apScroll < -textW) apScroll = PANEL_WIDTH;
  }
}

// ============================================================
// IO2 LED 燈號 Task（Core 0，與 Wi-Fi 同核）
// 負責根據 sysMode 產生對應閃爍模式
// ============================================================
//
// 閃爍模式對照：
//   SYS_BOOTING   : 快閃 100ms
//   SYS_USB_MSC   : 常亮
//   SYS_AP_WAIT   : 慢閃 1000ms
//   SYS_WIFI_CONN : 中速閃 500ms
//   SYS_WIFI_OK   : 熄滅
//   SYS_OTA       : 極快閃 50ms
//   SYS_RUNNING   : 熄滅
//
void ledTask(void *pvParameters) {
  pinMode(LED_IO2, OUTPUT);
  digitalWrite(LED_IO2, HIGH);  // 預設熄滅（低電位亮）

  while (true) {
    switch (sysMode) {
      case SYS_BOOTING:
        digitalWrite(LED_IO2, LOW);  delay(100);
        digitalWrite(LED_IO2, HIGH); delay(100);
        break;

      case SYS_USB_MSC:
        digitalWrite(LED_IO2, LOW);  // 常亮
        delay(50);
        break;

      case SYS_AP_WAIT:
        digitalWrite(LED_IO2, LOW);  delay(1000);
        digitalWrite(LED_IO2, HIGH); delay(1000);
        break;

      case SYS_WIFI_CONN:
        digitalWrite(LED_IO2, LOW);  delay(500);
        digitalWrite(LED_IO2, HIGH); delay(500);
        break;

      case SYS_OTA:
        digitalWrite(LED_IO2, LOW);  delay(50);
        digitalWrite(LED_IO2, HIGH); delay(50);
        break;

      case SYS_WIFI_OK:
      case SYS_RUNNING:
      default:
        digitalWrite(LED_IO2, HIGH); // 熄滅
        delay(100);
        break;
    }
  }
}

// ============================================================
// Render Task（Core 1）
// ============================================================
void renderTask(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  while (true) {
    // 非正常執行模式：顯示狀態畫面
    if (sysMode != SYS_RUNNING) {
      drawStatusScreen(sysMode);
      // 狀態畫面更新頻率：有跑馬燈時需快一點，否則 500ms 即可
      uint32_t delay_ms = (sysMode == SYS_AP_WAIT) ? 80 : 500;
      vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(delay_ms));
      continue;
    }

    // 正常執行：依 displayMode 顯示內容
    switch (state.displayMode) {
      case 0:
        drawClock();
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
        break;
      case 1:
        drawScrollText();
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(state.scrollSpeed));
        break;
      default:
        matrix->fillScreen(0);
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
        break;
    }
  }
}

// ============================================================
// WebSocket
// ============================================================
void broadcastStatus() {
  StaticJsonDocument<256> doc;
  doc["mode"]         = state.displayMode;
  doc["brightness"]   = state.brightness;
  doc["text"]         = state.scrollText;
  doc["speed"]        = state.scrollSpeed;
  doc["ntpSynced"]    = state.ntpSynced;
  doc["wifiConnected"]= state.wifiConnected;
  doc["usbMounted"]   = (bool)usbMounted;
  doc["fatfsOk"]      = (bool)fatfsMounted;
  String out; serializeJson(doc, out);
  ws.textAll(out);
}

void onWsEvent(AsyncWebSocket *srv, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    broadcastStatus();

  } else if (type == WS_EVT_DATA) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, data, len)) return;
    String cmd = doc["cmd"] | "";

    if      (cmd == "setMode") {
      state.displayMode = doc["value"] | 0;
    } else if (cmd == "setBrightness") {
      state.brightness = constrain((int)(doc["value"]|50), 0, 255);
      matrix->setBrightness8(state.brightness);
      prefs.putUChar("brightness", state.brightness);
    } else if (cmd == "setText") {
      state.scrollText = (const char*)(doc["value"] | "");
      state.scrollX = PANEL_WIDTH;
    } else if (cmd == "setSpeed") {
      state.scrollSpeed = constrain((int)(doc["value"]|40), 10, 200);
    } else if (cmd == "setWiFi") {
      String s = doc["ssid"]|"", p = doc["password"]|"";
      if (s.length()) {
        prefs.putString("ssid", s);
        prefs.putString("password", p);
        client->text("{\"status\":\"restarting\"}");
        delay(500); ESP.restart();
      }
    }
    client->text("{\"status\":\"ok\"}");
  }
}

// ============================================================
// Web Server
// ============================================================
void setupWebServer() {
  // 從 FATFS 提供靜態檔案
  // USB 存取中時 FATFS 已卸載，回傳 503
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (usbMounted) {
      req->send(503, "text/plain", "USB mode active. Please eject USB first.");
      return;
    }
    if (!fatfsMounted) {
      req->send(503, "text/plain", "Storage not ready.");
      return;
    }
    req->send(FFat, "/fatfs/index.html", "text/html");
  });

  server.serveStatic("/", FFat, "/fatfs/")
        .setCacheControl("no-cache");

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    StaticJsonDocument<256> doc;
    doc["mode"]       = state.displayMode;
    doc["brightness"] = state.brightness;
    doc["text"]       = state.scrollText;
    doc["ntpSynced"]  = state.ntpSynced;
    doc["ip"]         = state.wifiConnected ?
                        WiFi.localIP().toString() :
                        WiFi.softAPIP().toString();
    doc["apMode"]     = state.apMode;
    doc["usbMounted"] = (bool)usbMounted;
    doc["fatfsOk"]    = (bool)fatfsMounted;
    doc["fatfsFree"]  = fatfsMounted ? (uint32_t)(FFat.freeBytes()/1024) : 0;
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/wifi", HTTP_POST,
    [](AsyncWebServerRequest *req) {},
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len,
       size_t index, size_t total) {
      StaticJsonDocument<128> doc;
      deserializeJson(doc, data, len);
      String ssid = doc["ssid"]|"", pass = doc["password"]|"";
      if (ssid.length()) {
        prefs.putString("ssid", ssid);
        prefs.putString("password", pass);
        req->send(200, "application/json", "{\"status\":\"restarting\"}");
        delay(500); ESP.restart();
      } else {
        req->send(400, "application/json", "{\"error\":\"invalid ssid\"}");
      }
    }
  );

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
  Serial.println("Web server 啟動");
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32-S3 HUB75 LED Matrix [FATFS+MSC] ===");

  // USB MSC 需在 WiFi 之前初始化
  setupUSBMSC();

  // FATFS
  if (FFat.begin(true, FATFS_MOUNT, 10, FATFS_LABEL)) {
    fatfsMounted = true;
    Serial.printf("FATFS OK — 可用: %lu KB / 總計: %lu KB\n",
      FFat.freeBytes()/1024, FFat.totalBytes()/1024);
  } else {
    Serial.println("[錯誤] FATFS 掛載失敗");
  }

  // NVS
  prefs.begin("ledmatrix", false);
  state.ssid       = prefs.getString("ssid", "");
  state.password   = prefs.getString("password", "");
  state.brightness = prefs.getUChar("brightness", 50);

  // HUB75
  HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN);
  mxconfig.gpio.r1  = R1_PIN;  mxconfig.gpio.g1  = G1_PIN;
  mxconfig.gpio.b1  = B1_PIN;  mxconfig.gpio.r2  = R2_PIN;
  mxconfig.gpio.g2  = G2_PIN;  mxconfig.gpio.b2  = B2_PIN;
  mxconfig.gpio.a   = A_PIN;   mxconfig.gpio.b   = B_PIN;
  mxconfig.gpio.c   = C_PIN;   mxconfig.gpio.d   = D_PIN;
  mxconfig.gpio.e   = E_PIN;   mxconfig.gpio.lat = LAT_PIN;
  mxconfig.gpio.oe  = OE_PIN;  mxconfig.gpio.clk = CLK_PIN;
  mxconfig.clkphase = false;
  mxconfig.driver   = HUB75_I2S_CFG::FM6126A;

  matrix = new MatrixPanel_I2S_DMA(mxconfig);
  matrix->begin();
  matrix->setBrightness8(state.brightness);
  matrix->clearScreen();

  // ── LED Task 先啟動，讓燈號從開機就開始運作 ──────────────
  // 固定在 Core 0，與 Wi-Fi 同核，優先級低
  xTaskCreatePinnedToCore(
    ledTask, "LedTask",
    2048, nullptr, 1, nullptr, 0
  );

  // ── Render Task 啟動（Core 1）────────────────────────────
  // sysMode = SYS_BOOTING，面板顯示 BOOT / Init...
  xTaskCreatePinnedToCore(
    renderTask, "RenderTask",
    4096, nullptr, 2,
    &renderTaskHandle, 1
  );

  // ── Wi-Fi ─────────────────────────────────────────────────
  if (state.ssid.length()) {
    sysMode = SYS_WIFI_CONN;   // 面板：WiFi / Conn... / LED：中速閃
    state.wifiConnected = connectWiFi(state.ssid, state.password);
  }

  if (!state.wifiConnected) {
    startAP();
    sysMode = SYS_AP_WAIT;     // 面板：AP Mode / Setup / SSID跑馬燈 / LED：慢閃
  } else {
    sysMode = SYS_WIFI_OK;     // 面板：WiFi OK / NTP OK / LED：熄滅
  }

  // ── mDNS ──────────────────────────────────────────────────
  if (MDNS.begin(HOSTNAME))
    Serial.printf("mDNS: http://%s.local\n", HOSTNAME);

  // ── NTP ───────────────────────────────────────────────────
  if (state.wifiConnected) {
    syncNTP();
    // NTP 同步後短暫顯示成功狀態，再切入正常執行
    delay(1500);
  }

  // ── Web Server ────────────────────────────────────────────
  setupWebServer();

  // ── 進入正常執行 ──────────────────────────────────────────
  sysMode = SYS_RUNNING;       // LED 熄滅，面板開始顯示時鐘/跑馬燈

  Serial.println("系統啟動完成");
  Serial.println("── 使用說明 ──────────────────────────────");
  Serial.println("接上電腦 USB → 出現 LEDMATRIX 隨身碟");
  Serial.println("拖放 index.html / GIF 到隨身碟");
  Serial.println("安全退出隨身碟 → ESP32 自動重新掛載");
  Serial.printf ("瀏覽器開啟 http://%s.local\n", HOSTNAME);
  Serial.println("──────────────────────────────────────────");
}

// ============================================================
// Loop（Core 0）
// ============================================================
void loop() {
  ws.cleanupClients();

  // Wi-Fi 斷線重連（USB 存取中跳過）
  if (!state.apMode && !usbMounted &&
      WiFi.status() != WL_CONNECTED) {
    state.wifiConnected = false;
    sysMode = SYS_WIFI_CONN;   // 面板顯示連線中，LED 中速閃
    WiFi.reconnect();
    delay(5000);
    state.wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (state.wifiConnected) {
      sysMode = SYS_RUNNING;
      broadcastStatus();
    }
  }

  delay(100);
}
