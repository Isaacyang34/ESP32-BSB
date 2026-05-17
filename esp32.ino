#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <AnimatedGIF.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <queue>
#include <ArduinoJson.h>

// ==========================================
// 全域狀態與參數
// ==========================================
bool isSetupMode = false;
int scoreA = 0;
int scoreB = 0;
unsigned long lastActivityTime = 0;
bool testGifMode = false;
String testGifName = "";

// 數字版面與 GIF 設定參數
int bri = 64;
int sw = 10, sh = 20, st = 3, sp = 2; // 數字寬、高、粗細、間距
int sax = 2, sbx = 34, sy = 6;        // 兩隊分數起始座標
String gw = "", gls = "", gss = "";   // 勝利、落敗、待機 GIF 檔名
int glx = 0, grx = 0, gssx = 0;       // GIF X軸偏移量
int gifOffsetX = 0;
int gifOffsetY = 0;

Preferences prefs;

// ==========================================
// 面板設定
// ==========================================
#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1
MatrixPanel_I2S_DMA *dma_display = nullptr;

// ==========================================
// 語音模組設定 (CX1000AC)
// ==========================================
#define VOICE_RX_PIN 40
#define VOICE_TX_PIN 39
HardwareSerial VoiceSerial(1);
std::queue<int> voiceQueue;
unsigned long lastVoicePlayTime = 0;
const int VOICE_DELAY_MS = 1200;

void playVoiceTrack(int trackNo) {
  uint8_t cmd[9] = {0x7E, 0x09, 0xFF, 0xFF, 0x07, (uint8_t)((trackNo >> 8) & 0xFF), (uint8_t)(trackNo & 0xFF), 0x00, 0xEF};
  int sum = 0;
  for (int i = 0; i < 7; i++) sum += cmd[i];
  cmd[7] = (uint8_t)(sum & 0xFF);
  VoiceSerial.write(cmd, 9);
}

void queueScoreVoice() {
  voiceQueue.push(scoreA);
  voiceQueue.push(31); // 播放 "比"
  voiceQueue.push(scoreB);
}

// ==========================================
// GIF 播放器與檔案存取 (LittleFS)
// ==========================================
AnimatedGIF gif;
File currentGifFile;

void * GIFOpenFile(const char *fname, int32_t *pSize) {
  currentGifFile = LittleFS.open(fname, "r");
  if (currentGifFile) {
    *pSize = currentGifFile.size();
    return (void *)&currentGifFile;
  }
  return NULL;
}
void GIFCloseFile(void *pHandle) {
  File *f = static_cast<File *>(pHandle);
  if (f != NULL) f->close();
}
int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  int32_t iBytesRead = iLen;
  File *f = static_cast<File *>(pFile->fHandle);
  if (f && f->isDirectory() == false) iBytesRead = f->read(pBuf, iLen);
  return iBytesRead;
}
int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  File *f = static_cast<File *>(pFile->fHandle);
  if (f) f->seek(iPosition);
  return iPosition;
}
void GIFDraw(GIFDRAW *pDraw) {
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[128];
  int x, y, iWidth;

  iWidth = pDraw->iWidth;
  if (iWidth > PANEL_RES_X) iWidth = PANEL_RES_X;
  usPalette = pDraw->pPalette;
  y = pDraw->iY + pDraw->y + gifOffsetY; 
  
  if (y < 0 || y >= PANEL_RES_Y) return;

  s = pDraw->pPixels;
  if (pDraw->ucHasTransparency) {
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    x = 0;
    pEnd = s + iWidth;
    while (x < iWidth) {
      c = *ucTransparent-1;
      d = usTemp;
      while (c != ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent) { s--; }
        else { *d++ = usPalette[c]; }
      }
      int iCount = d - usTemp;
      if (iCount) {
        for(int i=0; i<iCount; i++) {
          int drawX = x + i + gifOffsetX;
          if(drawX >= 0 && drawX < PANEL_RES_X) dma_display->drawPixel(drawX, y, usTemp[i]);
        }
        x += iCount;
      }
      c = ucTransparent;
      while (c == ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent) x++;
        else s--;
      }
    }
  } else {
    for (x = 0; x < iWidth; x++) {
      int drawX = x + gifOffsetX;
      if(drawX >= 0 && drawX < PANEL_RES_X) dma_display->drawPixel(drawX, y, usPalette[*s++]);
    }
  }
}

// ==========================================
// 繪圖邏輯：客製化 7 段顯示器數字
// ==========================================
void drawDigitSegment(int x, int y, int w, int h, int t, int seg, uint16_t color) {
  switch(seg) {
    case 0: dma_display->fillRect(x, y, w, t, color); break; // Top
    case 1: dma_display->fillRect(x + w - t, y, t, h/2, color); break; // Top Right
    case 2: dma_display->fillRect(x + w - t, y + h/2, t, h/2, color); break; // Bottom Right
    case 3: dma_display->fillRect(x, y + h - t, w, t, color); break; // Bottom
    case 4: dma_display->fillRect(x, y + h/2, t, h/2, color); break; // Bottom Left
    case 5: dma_display->fillRect(x, y, t, h/2, color); break; // Top Left
    case 6: dma_display->fillRect(x, y + (h - t)/2, w, t, color); break; // Middle
  }
}

void drawDigit(int x, int y, int w, int h, int t, int digit, uint16_t color) {
  const byte segments[10] = {
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111  // 9
  };
  byte seg = segments[digit];
  for(int i=0; i<7; i++) {
    if(seg & (1<<i)) drawDigitSegment(x, y, w, h, t, i, color);
  }
}

void drawScoreboard() {
  dma_display->clearScreen();
  uint16_t colorA = dma_display->color565(255, 0, 0);
  uint16_t colorB = dma_display->color565(0, 255, 0);

  // 繪製 A 隊分數
  drawDigit(sax, sy, sw, sh, st, scoreA / 10, colorA);
  drawDigit(sax + sw + sp, sy, sw, sh, st, scoreA % 10, colorA);
  
  // 繪製 B 隊分數
  drawDigit(sbx, sy, sw, sh, st, scoreB / 10, colorB);
  drawDigit(sbx + sw + sp, sy, sw, sh, st, scoreB % 10, colorB);
}

// ==========================================
// 顯示專用 Task (Core 1)
// ==========================================
void renderTask(void *pvParameters) {
  gif.begin(GIF_PALETTE_RGB565_LE);
  while(true) {
    if (isSetupMode) {
      dma_display->clearScreen();
      dma_display->setTextSize(1);
      dma_display->setTextColor(dma_display->color565(0, 200, 255));
      dma_display->setCursor(2, 4); dma_display->print("WiFi Setup");
      dma_display->setCursor(2, 14); dma_display->print("Badminton_Setup");
      dma_display->setCursor(2, 24); dma_display->print("192.168.4.1");
      vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
      if (testGifMode && testGifName != "") {
        unsigned long testStart = millis();
        if (gif.open(("/" + testGifName).c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
          gifOffsetX = 0;
          while(gif.playFrame(true, NULL)) {
            if (millis() - testStart > 5000) break; // 測試播放 5 秒
            if (!testGifMode) break;
          }
          gif.close();
        }
        testGifMode = false;
      } else if (millis() - lastActivityTime > 15000 && gss != "") {
        // 閒置超過 15 秒且有設定待機動畫 -> 播放 Idle GIF
        if (gif.open(("/" + gss).c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
          gifOffsetX = gssx;
          while(gif.playFrame(true, NULL)) {
            if (millis() - lastActivityTime < 15000) break; // 有人操作就立刻中斷動畫
            if (testGifMode) break;
          }
          gif.close();
        } else {
          drawScoreboard();
          vTaskDelay(pdMS_TO_TICKS(100));
        }
      } else {
        // 正常顯示分數
        drawScoreboard();
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    }
  }
}

// ==========================================
// BLE 藍牙遙控功能
// ==========================================
#define SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
BLEServer *pServer = NULL;

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string rxValue = pCharacteristic->getValue();
      if (rxValue.length() > 0) {
        String cmd = String(rxValue.c_str());
        Serial.println("收到藍牙指令: " + cmd);
        lastActivityTime = millis(); // 喚醒動畫

        if (cmd == "A") { scoreA++; queueScoreVoice(); }
        else if (cmd == "B") { scoreB++; queueScoreVoice(); }
        else if (cmd == "A-") { if(scoreA>0) scoreA--; queueScoreVoice(); }
        else if (cmd == "B-") { if(scoreB>0) scoreB--; queueScoreVoice(); }
        else if (cmd == "R") { scoreA=0; scoreB=0; queueScoreVoice(); }
        else if (cmd == "SETUP") {
            prefs.putBool("setup_mode", true);
            delay(500);
            ESP.restart(); // 重啟進入 WiFi 設定模式
        }
      }
    }
};

void setupBLE() {
  BLEDevice::init("Badminton_Score_BLE");
  pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID_RX,
                                         BLECharacteristic::PROPERTY_WRITE
                                       );
  pRxCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("藍牙服務器已啟動");
}

// ==========================================
// WiFi AP 與 Web 網頁後台設定模式
// ==========================================
AsyncWebServer server(80);

const char setup_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<style>
  body{background:#121212;color:#eee;font-family:sans-serif;text-align:center;margin:0;padding:10px;}
  .card{background:#2a2a2a;margin-bottom:12px;padding:15px;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,0.5);text-align:left;}
  button{padding:10px;border-radius:8px;border:none;cursor:pointer;font-weight:bold;transition:0.1s;}
  .btn-sys{background:#e53935;color:white;width:100%;font-size:1.2rem;padding:15px;margin-bottom:10px;}
  .ctrl-row{display:flex;align-items:center;justify-content:space-between;margin:8px 0;background:#1e1e1e;padding:10px;border-radius:8px;}
  .preview-box{width:256px;height:128px;background:#000;border:2px solid #444;margin:10px auto;display:flex;align-items:center;justify-content:center;overflow:hidden;}
  .preview-box img{width:100%; height:100%; image-rendering:pixelated; object-fit:contain;}
  .btn-test{background:#28a745;color:white;width:100%;padding:15px;font-size:1.1rem;margin:10px 0;}
  input[type=text], select{padding:10px;background:#444;color:white;border:none;border-radius:5px;width:100%;box-sizing:border-box;}
  input[type=number] { padding:8px; background:#000; color:#0ff; border:1px solid #444; border-radius:5px; width:80px; text-align:center; font-family:monospace; font-size:1.2rem; }
</style></head><body>
  <button class="btn-sys" onclick="exitSetup()">💾 儲存並重啟回藍牙模式</button>
  
  <div class="card">
    <h3>📂 檔案預覽與管理</h3>
    <select id="fileSel" onchange="updatePreview()"></select>
    <div class="preview-box"><img id="preImg" src=""></div>
    <div style="margin-top:10px;display:flex;gap:5px;">
      <input type="text" id="newName" placeholder="新檔名.gif" style="width:65%;">
      <button style="background:#ffc107;width:35%;" onclick="renameFile()">更名</button>
    </div>
    <button class="btn-test" onclick="testOnMatrix()">▶️ 在面板測試播放 5 秒</button>
    <button style="background:#dc3545;color:white;width:100%;" onclick="deleteFile()">🗑️ 刪除此檔案</button>
  </div>

  <div class="card">
    <h3>🏆 動畫分配與定位設定</h3>
    上傳新 GIF: <input type="file" id="gifFile" accept=".gif" style="max-width:180px;"><button style="background:#17a2b8;color:white;margin-left:5px;" onclick="uploadGif()">上傳</button>
    <div id="upStatus" style="color:#0ff;font-size:0.9rem;margin-top:5px;"></div><hr>
    <div style="margin-bottom:10px;">🏆 勝利: <select id="gw" onchange="conf()"></select></div>
    <div style="margin-bottom:10px;">😭 落敗: <select id="gls" onchange="conf()"></select></div>
    <div style="margin-bottom:10px;">💤 待機: <select id="gss" onchange="conf()"></select></div>
    <hr>
    <div class="ctrl-row"><span>左方動畫 X 偏移:</span><input type="number" id="glx" onchange="conf()"></div>
    <div class="ctrl-row"><span>右方動畫 X 偏移:</span><input type="number" id="grx" onchange="conf()"></div>
    <div class="ctrl-row" style="border:1px solid #17a2b8;"><span>待機動畫 X 偏移:</span><input type="number" id="gssx" onchange="conf()"></div>
  </div>

  <div class="card">
    <h3 style="color:#0ff;">📐 數字版面佈局 (即時預覽)</h3>
    <div class="ctrl-row"><span>螢幕亮度 (10-255):</span><input type="range" id="bri" min="10" max="255" style="width:50%;" oninput="conf()" onchange="conf()"></div>
    <hr>
    <div class="ctrl-row"><span>數字寬度:</span><input type="number" id="sw" onchange="conf()"></div>
    <div class="ctrl-row"><span>數字高度:</span><input type="number" id="sh" onchange="conf()"></div>
    <div class="ctrl-row"><span>線條粗細:</span><input type="number" id="st" onchange="conf()"></div>
    <div class="ctrl-row"><span>數字間距:</span><input type="number" id="sp" onchange="conf()"></div>
    <div class="ctrl-row"><span>左方起點:</span><input type="number" id="sax" onchange="conf()"></div>
    <div class="ctrl-row"><span>右方起點:</span><input type="number" id="sbx" onchange="conf()"></div>
    <div class="ctrl-row"><span>全體 Y 偏:</span><input type="number" id="sy" onchange="conf()"></div>
  </div>

<script>
window.onload = function() {
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('bri').value = d.bri;
    const keys = ['sw','sh','st','sp','sax','sbx','sy','glx','grx','gssx'];
    keys.forEach(k => { if(document.getElementById(k)) document.getElementById(k).value = d[k]; });
    refreshFileList(d.gw, d.gls, d.gss);
  });
};

function refreshFileList(gw, gls, gss) {
  fetch('/listGifs').then(r=>r.json()).then(arr => {
    let opts = '<option value="">-- 選擇檔案 --</option>';
    arr.forEach(f => opts += `<option value="${f}">${f}</option>`);
    document.getElementById('fileSel').innerHTML = opts;
    document.getElementById('gw').innerHTML = opts; document.getElementById('gls').innerHTML = opts; document.getElementById('gss').innerHTML = opts;
    if(arr.includes(gw)) document.getElementById('gw').value = gw; 
    if(arr.includes(gls)) document.getElementById('gls').value = gls; 
    if(arr.includes(gss)) document.getElementById('gss').value = gss;
  });
}

function updatePreview() {
  const f = document.getElementById('fileSel').value;
  document.getElementById('preImg').src = f ? `/getGif?name=${f}` : '';
}

function renameFile() {
  const oldN = document.getElementById('fileSel').value;
  const newN = document.getElementById('newName').value;
  if(!oldN || !newN) return alert("請選檔案並輸入新名稱(需包含.gif)");
  fetch(`/rename?old=${oldN}&new=${newN}`).then(r => r.text()).then(t => { alert(t); location.reload(); });
}

function deleteFile() {
  const f = document.getElementById('fileSel').value;
  if(!f || !confirm("確定刪除 "+f+" ？")) return;
  fetch(`/delete?name=${f}`).then(() => location.reload());
}

function testOnMatrix() {
  const f = document.getElementById('fileSel').value;
  if(!f) return alert("請先選擇檔案");
  fetch(`/test?name=${f}`).then(r => {
      if(!r.ok) r.text().then(t => alert("播放失敗: " + t));
  });
}

function conf(){ 
  const keys = ['sw','sh','st','sp','sax','sbx','sy','glx','grx','gssx'];
  let q = `/config?b=${document.getElementById('bri').value}&gw=${document.getElementById('gw').value}&gls=${document.getElementById('gls').value}&gss=${document.getElementById('gss').value}`;
  keys.forEach(k => { q += `&${k}=${document.getElementById(k).value}`; });
  fetch(q); 
}

function uploadGif() {
  const file = document.getElementById('gifFile').files[0]; if(!file) return alert('請選擇檔案');
  const fd = new FormData(); fd.append('file', file); document.getElementById('upStatus').innerText = '⏳ 上傳中...';
  fetch('/upload', {method: 'POST', body: fd}).then(r => { if(r.ok) { alert('上傳成功'); location.reload(); } });
}

function exitSetup() { if(confirm("重啟回藍牙模式？")) fetch('/exit').then(() => { alert("重啟中..."); }); }
</script></body></html>
)rawliteral";

void loadConfig() {
  bri = prefs.getInt("bri", 64);
  sw = prefs.getInt("sw", 10);
  sh = prefs.getInt("sh", 20);
  st = prefs.getInt("st", 3);
  sp = prefs.getInt("sp", 2);
  sax = prefs.getInt("sax", 2);
  sbx = prefs.getInt("sbx", 34);
  sy = prefs.getInt("sy", 6);
  gw = prefs.getString("gw", "");
  gls = prefs.getString("gls", "");
  gss = prefs.getString("gss", "");
  glx = prefs.getInt("glx", 0);
  grx = prefs.getInt("grx", 0);
  gssx = prefs.getInt("gssx", 0);
}

void setupWiFiAndServer() {
  WiFi.softAP("Badminton_Setup", "12345678");
  Serial.println("WiFi AP Started: Badminton_Setup");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", setup_html);
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    StaticJsonDocument<512> doc;
    doc["bri"] = bri; doc["sw"] = sw; doc["sh"] = sh; doc["st"] = st; doc["sp"] = sp;
    doc["sax"] = sax; doc["sbx"] = sbx; doc["sy"] = sy;
    doc["gw"] = gw; doc["gls"] = gls; doc["gss"] = gss;
    doc["glx"] = glx; doc["grx"] = grx; doc["gssx"] = gssx;
    String out; serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/listGifs", HTTP_GET, [](AsyncWebServerRequest *request){
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    StaticJsonDocument<1024> doc;
    JsonArray arr = doc.to<JsonArray>();
    while(file) {
      String fn = String(file.name());
      if (fn.endsWith(".gif")) arr.add(fn);
      file = root.openNextFile();
    }
    String out; serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("b")) { bri = request->getParam("b")->value().toInt(); prefs.putInt("bri", bri); dma_display->setBrightness8(bri); }
    if(request->hasParam("sw")) { sw = request->getParam("sw")->value().toInt(); prefs.putInt("sw", sw); }
    if(request->hasParam("sh")) { sh = request->getParam("sh")->value().toInt(); prefs.putInt("sh", sh); }
    if(request->hasParam("st")) { st = request->getParam("st")->value().toInt(); prefs.putInt("st", st); }
    if(request->hasParam("sp")) { sp = request->getParam("sp")->value().toInt(); prefs.putInt("sp", sp); }
    if(request->hasParam("sax")) { sax = request->getParam("sax")->value().toInt(); prefs.putInt("sax", sax); }
    if(request->hasParam("sbx")) { sbx = request->getParam("sbx")->value().toInt(); prefs.putInt("sbx", sbx); }
    if(request->hasParam("sy")) { sy = request->getParam("sy")->value().toInt(); prefs.putInt("sy", sy); }
    if(request->hasParam("gw")) { gw = request->getParam("gw")->value(); prefs.putString("gw", gw); }
    if(request->hasParam("gls")) { gls = request->getParam("gls")->value(); prefs.putString("gls", gls); }
    if(request->hasParam("gss")) { gss = request->getParam("gss")->value(); prefs.putString("gss", gss); }
    if(request->hasParam("glx")) { glx = request->getParam("glx")->value().toInt(); prefs.putInt("glx", glx); }
    if(request->hasParam("grx")) { grx = request->getParam("grx")->value().toInt(); prefs.putInt("grx", grx); }
    if(request->hasParam("gssx")) { gssx = request->getParam("gssx")->value().toInt(); prefs.putInt("gssx", gssx); }
    
    // 即時預覽數字畫面
    testGifMode = false;
    
    request->send(200, "text/plain", "OK");
  });

  server.on("/getGif", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("name")) {
      String fn = "/" + request->getParam("name")->value();
      request->send(LittleFS, fn, "image/gif");
    } else {
      request->send(400, "text/plain", "Missing name");
    }
  });

  server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("name")) {
      testGifName = request->getParam("name")->value();
      testGifMode = true;
      request->send(200, "text/plain", "Playing");
    } else {
      request->send(400, "text/plain", "Missing name");
    }
  });

  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("name")) {
      String fn = "/" + request->getParam("name")->value();
      LittleFS.remove(fn);
      request->send(200, "text/plain", "Deleted");
    } else {
      request->send(400, "text/plain", "Missing name");
    }
  });

  server.on("/rename", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("old") && request->hasParam("new")) {
      String o = "/" + request->getParam("old")->value();
      String n = "/" + request->getParam("new")->value();
      LittleFS.rename(o, n);
      request->send(200, "text/plain", "Renamed");
    } else {
      request->send(400, "text/plain", "Missing params");
    }
  });

  server.on("/exit", HTTP_GET, [](AsyncWebServerRequest *request){
    prefs.putBool("setup_mode", false);
    request->send(200, "text/plain", "Rebooting...");
    delay(500);
    ESP.restart();
  });

  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Uploaded");
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if(!index){
      request->_tempFile = LittleFS.open("/" + filename, "w");
    }
    if(request->_tempFile){
      if(len){
        request->_tempFile.write(data, len);
      }
      if(final){
        request->_tempFile.close();
      }
    }
  });

  server.begin();
}

// ==========================================
// Setup & Loop
// ==========================================
void setup() {
  Serial.begin(115200);

  // 初始化檔案系統 LittleFS
  if(!LittleFS.begin(true)) {
    Serial.println("LittleFS 初始化失敗");
  }

  // 讀取設定與狀態
  prefs.begin("system", false);
  isSetupMode = prefs.getBool("setup_mode", false);
  loadConfig();

  // 初始化語音模組
  VoiceSerial.begin(9600, SERIAL_8N1, VOICE_RX_PIN, VOICE_TX_PIN);

  // 初始化 LED 矩陣面板
  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(bri);
  dma_display->clearScreen();

  lastActivityTime = millis();

  // 根據狀態開啟 BLE 或是 WiFi
  if (isSetupMode) {
    setupWiFiAndServer();
  } else {
    setupBLE();
  }

  // 啟動繪圖任務 (固定在 Core 1)
  xTaskCreatePinnedToCore(
    renderTask,
    "RenderTask",
    4096,
    NULL,
    1,
    NULL,
    1
  );
}

void loop() {
  // 處理語音非阻塞播放佇列 (Core 0)
  if (!voiceQueue.empty()) {
    if (millis() - lastVoicePlayTime > VOICE_DELAY_MS) {
      int track = voiceQueue.front();
      voiceQueue.pop();
      playVoiceTrack(track);
      lastVoicePlayTime = millis();
    }
  }
  delay(10);
}