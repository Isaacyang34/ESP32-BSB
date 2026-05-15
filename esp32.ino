#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <queue>

// ==========================================
// 語音模組設定 (CX1000AC)
// ==========================================
#define VOICE_RX_PIN 40 // ESP32 RX (接模組 TX)
#define VOICE_TX_PIN 39 // ESP32 TX (接模組 RX)
HardwareSerial VoiceSerial(1);

std::queue<int> voiceQueue;
unsigned long lastVoicePlayTime = 0;
const int VOICE_DELAY_MS = 1200; // 每個語音播放的間隔時間(毫秒)，可依據實際語音長度調整

void playVoiceTrack(int trackNo) {
  // CX1000AC / WT2003S 播放指令 (0x7E 0x09 ...)
  uint8_t cmd[9] = {0x7E, 0x09, 0xFF, 0xFF, 0x07, (uint8_t)((trackNo >> 8) & 0xFF), (uint8_t)(trackNo & 0xFF), 0x00, 0xEF};
  int sum = 0;
  for (int i = 0; i < 7; i++) {
    sum += cmd[i];
  }
  cmd[7] = (uint8_t)(sum & 0xFF);
  VoiceSerial.write(cmd, 9);
  Serial.printf("播放語音檔索引: %d\n", trackNo);
}

// ==========================================
// 1. WiFi 設定 (開啟 AP 基地台模式)
// ==========================================
const char* ssid = "Badminton_Score";
const char* password = "password123"; // 密碼至少8碼

// 建立非同步伺服器 (Port 80)
AsyncWebServer server(80);

// ==========================================
// 2. LED 矩陣面板設定 (HUB75)
// ==========================================
#define PANEL_RES_X 64  // 依照你的 P6 面板寬度修改 (通常是 64)
#define PANEL_RES_Y 32  // 依照你的 P6 面板高度修改 (通常是 32 或 64)
#define PANEL_CHAIN 1   // 串接數量
MatrixPanel_I2S_DMA *dma_display = nullptr;

// ==========================================
// 3. 儲存全域狀態 (用來更新畫面)
// ==========================================
int scoreA = 0;
int scoreB = 0;
String serveTeam = "A";
String pA_Left = "", pA_Right = "";
String pB_Left = "", pB_Right = "";

// ==========================================
// 4. 將 HTML 網頁存入 C++ 變數 (使用 PROGMEM 節省 RAM)
// ==========================================
// 注意：請將你在上面看到的 index.html 完整內容，貼到下面兩個 rawliteral 標記之間。
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-TW">
<!-- 這裡貼上完整的 HTML 原始碼 -->
<!-- 為了節省篇幅，請自行從上方 index.html 複製貼上到這個區塊內 -->
</html>
)rawliteral";


void drawScoreboard() {
  if (!dma_display) return;
  dma_display->clearScreen();

  // 這裡是你自由發揮 P6 LED 繪圖的地方
  // 示範繪製分數 (簡單文字範例)
  dma_display->setTextSize(2); // 放大字體
  dma_display->setTextWrap(false);

  // A 隊 (紅色)
  dma_display->setTextColor(dma_display->color565(255, 0, 0));
  dma_display->setCursor(2, 2);
  dma_display->printf("%02d", scoreA);
  
  // B 隊 (綠色)
  dma_display->setTextColor(dma_display->color565(0, 255, 0));
  dma_display->setCursor(34, 2);
  dma_display->printf("%02d", scoreB);

  // 標示發球方 (例如畫一個小白點或底線)
  if (serveTeam == "A") {
     dma_display->fillRect(2, 20, 24, 2, dma_display->color565(255, 255, 255));
  } else {
     dma_display->fillRect(34, 20, 24, 2, dma_display->color565(255, 255, 255));
  }

  // 若要顯示中文名字，需要掛載中文字庫 (如 U8g2_for_Adafruit_GFX)
  // 目前僅作為測試，將狀態印在 Serial 上
}

void setup() {
  Serial.begin(115200);

  // 初始化語音模組序列埠
  VoiceSerial.begin(9600, SERIAL_8N1, VOICE_RX_PIN, VOICE_TX_PIN);
  Serial.println("Voice Serial Init Done");

  // 初始化 LED 面板
  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
  // mxconfig.gpio.e = 18; // 如果你的面板是 64x64 需要指定 E pin
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(64); // 設定亮度 0-255
  dma_display->clearScreen();
  
  // 顯示開機文字
  dma_display->setTextColor(dma_display->color565(0, 200, 255));
  dma_display->setCursor(5, 10);
  dma_display->print("WiFi...");

  // 設定 WiFi AP (手機連上這個 WiFi 即可控制)
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP); // 預設通常為 192.168.4.1

  // 開機完成畫面
  dma_display->clearScreen();
  dma_display->setCursor(2, 10);
  dma_display->print("IP: 4.1");

  // ==========================================
  // Web Server 路由設定
  // ==========================================
  
  // 當使用者在瀏覽器輸入 192.168.4.1 時，回傳 HTML
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // 接收前端丟過來的 HTTP GET API (/update?scoreA=...&scoreB=...)
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    
    // 解析 URL 參數
    if(request->hasParam("scoreA")) scoreA = request->getParam("scoreA")->value().toInt();
    if(request->hasParam("scoreB")) scoreB = request->getParam("scoreB")->value().toInt();
    if(request->hasParam("serve"))  serveTeam = request->getParam("serve")->value();
    
    // 如果有中文球員名稱，這裡收到的是 UTF-8 字串
    if(request->hasParam("al")) pA_Left  = request->getParam("al")->value();
    if(request->hasParam("ar")) pA_Right = request->getParam("ar")->value();
    if(request->hasParam("bl")) pB_Left  = request->getParam("bl")->value();
    if(request->hasParam("br")) pB_Right = request->getParam("br")->value();

    Serial.printf("A:%02d B:%02d 發球:%s \n", scoreA, scoreB, serveTeam.c_str());
    Serial.printf("A隊:[%s, %s] B隊:[%s, %s]\n", pA_Left.c_str(), pA_Right.c_str(), pB_Left.c_str(), pB_Right.c_str());

    // 呼叫繪圖函式更新 LED
    drawScoreboard();

    // 將報分加入語音佇列 (A分數 -> "比" -> B分數)
    // 依據您的設定：0分 對應 00000.mp3，1分 對應 00001.mp3... (檔名 = 分數)
    voiceQueue.push(scoreA); 
    voiceQueue.push(31);         // "比" (00031.mp3)
    voiceQueue.push(scoreB);

    // 回傳成功給手機端
    request->send(200, "text/plain", "OK");
  });

  // 啟動伺服器
  server.begin();
  Serial.println("HTTP Server Started");
}

void loop() {
  // 處理語音非阻塞播放佇列
  if (!voiceQueue.empty()) {
    if (millis() - lastVoicePlayTime > VOICE_DELAY_MS) {
      int track = voiceQueue.front();
      voiceQueue.pop();
      playVoiceTrack(track);
      lastVoicePlayTime = millis();
    }
  }
  
  // 可以在這裡放些跑馬燈動畫，或者保持空白
}