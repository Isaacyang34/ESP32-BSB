/* ========================================================
 * 羽球記分板全功能韌體 (2025-05-05 外部網頁控制對齊版)
 * ========================================================
 * 修正項目：
 * 1. 移除音量藍牙控制與視覺顯示：完全對齊 isaacyang34 網頁控制端。
 * 2. 修正 AnimatedGIF::open 參數：補齊 6 個回調參數解決編譯錯誤。
 * 3. 強化 BLE 穩定性：實作斷線自動重啟廣播，並關閉 WiFi 射頻干擾。
 * 4. 語音精確調校：保留 VOICE_GAP_MS 與 VOICE_STOP_DELAY_MS 參數。
 * ======================================================== */

#include <Preferences.h>
#include <FS.h>
#include <FFat.h>
#include <AnimatedGIF.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <WebServer.h>

// BLE 核心函式庫
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

/* --- 📋 課長專屬調校參數區 --- */
#define VOICE_VOLUME        25     // 固定開機音量 (0~30)
#define VOICE_GAP_MS        1000   // [分數] 到 [比]、[比] 到 [下個分數] 的間隔 (ms)
#define VOICE_STOP_DELAY_MS 1500   // 最後一段分報完後，隔多久才送 Stop 指令 (ms)
#define VOICE_MODE          0x00   // 播放模式：0x00 通常為單曲停止
#define DEBOUNCE_MS         450    // 藍牙指令去彈跳時間 (ms)

/* --- 硬體腳位定義 --- */
#define CLK_PIN 10
#define LAT_PIN 11
#define OE_PIN  12
#define E_PIN   14
#define A_PIN   17
#define B_PIN   18
#define C_PIN   8
#define D_PIN   9
#define R1_PIN  4
#define G1_PIN  5
#define B1_PIN  6
#define R2_PIN  7
#define G2_PIN  15
#define B2_PIN  16

// 語音模組 (CX1000AM / WT2003S 協定)
#define PIN_AUDIO_TX   39
#define PIN_AUDIO_RX   40
#define PIN_AUDIO_BUSY 38

/* --- 全域變數 --- */
Preferences prefs;
MatrixPanel_I2S_DMA *display = nullptr;
WebServer server(80);

bool isWifiMode = false; 
int scoreA = 0, scoreB = 0;
String serveTeam = "A"; 
bool isGameOver = false;
unsigned long lastActionTime = 0;
unsigned long lastBleCommandTime = 0; 

int brightness = 100, panel_w = 64, panel_h = 32;       
int segW = 6, segH = 22, segThick = 1, segSpace = 2; 
int scoreAxOff = -15, scoreBxOff = 2, scoreYOff = -11;  

AnimatedGIF gifL, gifR;
bool gifL_open = false, gifR_open = false;
String gifWin_name = "", gifLose_name = "", gifSS_name = ""; 
int gifLx = 0, gifRx = 32, gifSSx = 0;                  
unsigned long nextFrameL = 0, nextFrameR = 0;
unsigned long testEndTime = 0;
bool needsUpdateUI = true;

bool bleDoReset = false, bleCheckScore = false, inScreenSaver = false;
bool deviceConnected = false; 
String activeGifL_path = "", activeGifR_path = "", activeGifTest_path = "";

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

/* --- 語音報分狀態機 --- */
enum ScoreState { STATE_WAITING, STATE_PLAY_A, STATE_WAIT_GAP_1, STATE_PLAY_BI, STATE_WAIT_GAP_2, STATE_PLAY_B, STATE_WAIT_STOP_CMD };
ScoreState audioState = STATE_WAITING;
int targetScoreFirst = 0, targetScoreSecond = 0;
HardwareSerial AudioSerial(1); 
unsigned long nextVoiceActionTime = 0; 

/* --- 函式宣告 --- */
void closeGIFs();
void * GIFOpenFile(const char *fn, int32_t *s);
void GIFCloseFile(void *h);
int32_t GIFReadFile(GIFFILE *p, uint8_t *b, int32_t l);
int32_t GIFSeekFile(GIFFILE *p, int32_t i);
void GIFDraw(GIFDRAW *p);
void GIFDrawSS(GIFDRAW *p);
void initDisplay();
void drawScore(); 
void setVolume(byte vol);
void sendPlayCommand(int trackIndex);
void sendStopCommand();
void setSingleMode();
void startScoreAnnouncement(int s1, int s2);
String safePath(String name);

/* --- 🎵 語音通訊底層控制 --- */
void sendAudioCmd(byte* cmd, int len) {
    int sum = 0;
    for (int i = 0; i < len - 2; i++) sum += cmd[i];
    cmd[len - 2] = (byte)(sum & 0xFF);
    AudioSerial.write(cmd, len); 
    AudioSerial.flush();
    Serial.print("[TX] ");
    for(int i=0; i<len; i++) { Serial.printf("%02X ", cmd[i]); }
    Serial.println();
}

void setVolume(byte vol) {
    byte cmd[] = {0x7E, 0x08, 0xFF, 0xFF, 0x13, (byte)vol, 0x00, 0xEF};
    sendAudioCmd(cmd, sizeof(cmd));
}

void sendStopCommand() {
    byte cmd[] = {0x7E, 0x07, 0xFF, 0xFF, 0x04, 0x00, 0xEF};
    sendAudioCmd(cmd, sizeof(cmd));
}

void sendPlayCommand(int trackIndex) {
    byte h = (trackIndex >> 8) & 0xFF, l = trackIndex & 0xFF;
    byte cmd[] = {0x7E, 0x09, 0xFF, 0xFF, 0x07, h, l, 0x00, 0xEF};
    sendAudioCmd(cmd, sizeof(cmd));
}

void setSingleMode() {
    byte cmd[] = {0x7E, 0x0A, 0xFF, 0xFF, 0x18, VOICE_MODE, 0x00, 0x00, 0x00, 0xEF};
    sendAudioCmd(cmd, sizeof(cmd));
}

void startScoreAnnouncement(int s1, int s2) {
    if (audioState != STATE_WAITING) return;
    targetScoreFirst = s1; targetScoreSecond = s2;
    audioState = STATE_PLAY_A;
}

void processAudioState() {
    unsigned long now = millis();
    if (audioState != STATE_WAITING && now < nextVoiceActionTime) return;

    switch (audioState) {
        case STATE_PLAY_A:
            sendPlayCommand(targetScoreFirst + 1);
            nextVoiceActionTime = now + VOICE_GAP_MS; 
            audioState = STATE_WAIT_GAP_1;
            break;
        case STATE_WAIT_GAP_1:
            audioState = STATE_PLAY_BI;
            break;
        case STATE_PLAY_BI:
            sendPlayCommand(32); 
            nextVoiceActionTime = now + VOICE_GAP_MS;
            audioState = STATE_WAIT_GAP_2;
            break;
        case STATE_WAIT_GAP_2:
            audioState = STATE_PLAY_B;
            break;
        case STATE_PLAY_B:
            sendPlayCommand(targetScoreSecond + 1);
            nextVoiceActionTime = now + VOICE_STOP_DELAY_MS;
            audioState = STATE_WAIT_STOP_CMD;
            break;
        case STATE_WAIT_STOP_CMD:
            sendStopCommand(); 
            audioState = STATE_WAITING;
            break;
        default: break;
    }
}

/* --- 網頁 HTML 介面 --- */
const char setup_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
<style>
  body{background:#121212;color:#eee;font-family:sans-serif;text-align:center;margin:0;padding: env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left);}
  .card{background:#2a2a2a;margin-bottom:12px;padding:15px;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,0.5);text-align:left;}
  button{padding:10px;border-radius:8px;border:none;cursor:pointer;font-weight:bold;}
  .btn-sys{background:#e53935;color:white;width:100%;font-size:1.2rem;padding:15px;margin-bottom:10px;}
  input[type=text], select{padding:10px;background:#444;color:white;border:none;border-radius:5px;width:100%;box-sizing:border-box;}
</style></head><body>
  <button class="btn-sys" onclick="exitSetup()">💾 儲存並重啟回到藍牙模式</button>
  <div class="card"><h3>📂 檔案管理</h3><select id="fileSel" onchange="updatePreview()"></select>
    <div style="margin-top:10px;display:flex;gap:5px;"><input type="text" id="newName" placeholder="新名稱.gif" style="width:65%;"><button style="background:#ffc107;" onclick="renameFile()">更名</button></div>
    <button style="background:#dc3545;color:white;width:100%;margin-top:10px;" onclick="deleteFile()">🗑️ 刪除</button></div>
<script>
function refreshFileList(gw, gls, gss) {
  fetch('/listGifs').then(r=>r.json()).then(arr => {
    let opts = '<option value="">-- 選擇 --</option>'; arr.forEach(f => opts += `<option value="${f}">${f}</option>`);
    ['fileSel','gw','gls','gss'].forEach(id => { const el=document.getElementById(id); if(el) el.innerHTML=opts; });
  });
}
window.onload = () => { fetch('/status').then(r=>r.json()).then(d=>{ refreshFileList(d.gw, d.gls, d.gss); }); };
function exitSetup() { fetch('/exit').then(()=>alert("正在重啟，請稍後重新搜尋藍牙...")); }
</script></body></html>)rawliteral";

/* --- 🛡️ BLE Server 回調：處理斷線重新廣播 --- */
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; Serial.println("[BLE] 連線成功"); };
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("[BLE] 連線中斷，重新啟動廣播以供回連...");
        pServer->getAdvertising()->start(); 
    }
};

/* --- BLE 數據回調：處理按鍵（與外部網頁對齊） --- */
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pC) {
        std::string rawValue = pC->getValue();
        unsigned long now = millis();
        if (rawValue.length() == 0 || (now - lastBleCommandTime < DEBOUNCE_MS)) return;
        
        String rxValue = String(rawValue.c_str()); rxValue.trim(); rxValue.toUpperCase();
        lastBleCommandTime = now; lastActionTime = now;
        
        if(rxValue == "R") { bleDoReset = true; } 
        else if(rxValue == "SETUP") { prefs.putBool("wifi_mode", true); delay(500); ESP.restart(); }
        else {
            // ✅ 計分邏輯：對齊網頁發送的 A, B, A-, B-
            if (rxValue == "A") { scoreA++; serveTeam="A"; startScoreAnnouncement(scoreA, scoreB); }
            else if (rxValue == "B") { scoreB++; serveTeam="B"; startScoreAnnouncement(scoreB, scoreA); }
            else if (rxValue == "A-") { if(scoreA > 0) scoreA--; }
            else if (rxValue == "B-") { if(scoreB > 0) scoreB--; }
            bleCheckScore = true; 
        }
        if (deviceConnected) {
            String status = "S:" + String(scoreA) + "-" + String(scoreB);
            pTxCharacteristic->setValue(status.c_str()); pTxCharacteristic->notify();
        }
    }
};

void setup() {
    Serial.begin(115200); Serial.setTxTimeoutMs(0); delay(1000); 
    
    initDisplay();
    if(display) { display->clearScreen(); display->setBrightness8(100); drawScore(); }

    if(!FFat.begin(true)) Serial.println("[ERR] FFat 失敗");

    AudioSerial.begin(9600, SERIAL_8N1, PIN_AUDIO_RX, PIN_AUDIO_TX);
    while(AudioSerial.available()) AudioSerial.read(); AudioSerial.flush();
    setVolume(VOICE_VOLUME); delay(150);
    setSingleMode(); delay(150);
    sendStopCommand();

    prefs.begin("scoreboard", false);
    isWifiMode = prefs.getBool("wifi_mode", false);
    brightness = prefs.getInt("bri", 100); segW = prefs.getInt("sw", 6); segH = prefs.getInt("sh", 22); 
    segThick = prefs.getInt("st", 1); segSpace = prefs.getInt("sp", 2);
    scoreAxOff = prefs.getInt("sax", -15); scoreBxOff = prefs.getInt("sbx", 2); scoreYOff = prefs.getInt("sy", -11);
    gifWin_name = prefs.getString("gw", ""); gifLose_name = prefs.getString("gls", ""); gifSS_name = prefs.getString("gss", "");
    gifLx = prefs.getInt("glx", 0); gifRx = prefs.getInt("grx", 32); gifSSx = prefs.getInt("gssx", 0);

    if (isWifiMode) {
        WiFi.softAP("Badminton_Setup", "12345678");
        server.on("/", [](){ server.send(200, "text/html", setup_html); });
        server.on("/status", [](){ String j = "{\"bri\":"+String(brightness)+",\"gw\":\""+gifWin_name+"\",\"gls\":\""+gifLose_name+"\",\"gss\":\""+gifSS_name+"\",\"sw\":"+String(segW)+",\"sh\":"+String(segH)+",\"st\":"+String(segThick)+",\"sp\":"+String(segSpace)+",\"sax\":"+String(scoreAxOff)+",\"sbx\":"+String(scoreBxOff)+",\"sy\":"+String(scoreYOff)+",\"glx\":"+String(gifLx)+",\"grx\":"+String(gifRx)+",\"gssx\":"+String(gifSSx)+"}"; server.send(200, "application/json", j); });
        server.on("/exit", [](){ prefs.putBool("wifi_mode", false); server.send(200, "text/plain", "Restarting..."); delay(1000); ESP.restart(); });
        server.on("/listGifs", [](){ String j = "["; File root = FFat.open("/"); File f = root.openNextFile(); bool first=true; while(f){ String n = f.name(); if(n.endsWith(".gif")){ if(!first) j+=","; j+="\""+n+"\""; first=false; } f=root.openNextFile(); } j+="]"; server.send(200, "application/json", j); });
        server.on("/upload", HTTP_POST, [](){ server.send(200); }, [](){ HTTPUpload& u = server.upload(); static File f; if(u.status == UPLOAD_FILE_START) f = FFat.open(safePath(u.filename), FILE_WRITE); else if(u.status == UPLOAD_FILE_WRITE) f.write(u.buf, u.currentSize); else if(u.status == UPLOAD_FILE_END) f.close(); });
        server.begin();
    } else {
        WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
        BLEDevice::init("Badminton_Score_BLE"); 
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        BLEService *pS = pServer->createService(SERVICE_UUID);
        pTxCharacteristic = pS->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
        pTxCharacteristic->addDescriptor(new BLE2902());
        BLECharacteristic *pRx = pS->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
        pRx->setCallbacks(new MyCharacteristicCallbacks()); 
        pS->start(); pServer->getAdvertising()->start();
    }
    delay(500); startScoreAnnouncement(0, 0); 
}

void loop() {
    processAudioState(); unsigned long now = millis();
    
    if (isWifiMode) { server.handleClient(); } else {
        if (bleDoReset) { scoreA=0; scoreB=0; isGameOver=false; inScreenSaver=false; closeGIFs(); if(display) display->clearScreen(); needsUpdateUI=true; bleDoReset=false; startScoreAnnouncement(0, 0); }
        if (bleCheckScore) {
            bool go = ((scoreA>=21||scoreB>=21)&&(abs(scoreA-scoreB)>=2||scoreA==30||scoreB==30));
            if (go && !isGameOver) {
                isGameOver=true; if(display) display->clearScreen();
                activeGifL_path = safePath((scoreA > scoreB) ? gifWin_name : gifLose_name); activeGifR_path = safePath((scoreB > scoreA) ? gifWin_name : gifLose_name);
                gifL_open = gifL.open(activeGifL_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw);
                gifR_open = gifR.open(activeGifR_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw);
            } else if (!go && isGameOver) { isGameOver=false; closeGIFs(); if(display) display->clearScreen(); }
            needsUpdateUI=true; bleCheckScore=false;
        }
        if (now - lastActionTime > 180000 && !isGameOver) {
            if (!inScreenSaver) { inScreenSaver=true; closeGIFs(); if(display) display->clearScreen(); activeGifTest_path=safePath(gifSS_name); if(activeGifTest_path!="/") gifL_open=gifL.open(activeGifTest_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDrawSS); nextFrameL=now; }
            if (gifL_open && now >= nextFrameL) { int d=0; if (gifL.playFrame(false, &d, NULL)) nextFrameL = now + (d<20?100:d); else { gifL.close(); gifL.open(activeGifTest_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDrawSS); nextFrameL = now+50; } }
        } else {
            if (inScreenSaver) { inScreenSaver=false; closeGIFs(); if(display) display->clearScreen(); needsUpdateUI=true; }
            if (isGameOver) {
                if(gifL_open && now>=nextFrameL){ int d=0; if(gifL.playFrame(false,&d,(void*)0)) nextFrameL=now+(d<20?100:d); else { gifL.close(); gifL.open(activeGifL_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw); nextFrameL=now+50; } }
                if(gifR_open && now>=nextFrameR){ int d=0; if(gifR.playFrame(false,&d,(void*)1)) nextFrameR=now+(d<20?100:d); else { gifR.close(); gifR.open(activeGifR_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw); nextFrameR=now+50; } }
            } else if(needsUpdateUI) { drawScore(); needsUpdateUI=false; }
        }
    }
}

/* --- 工具實作 --- */
void initDisplay() { HUB75_I2S_CFG cfg(panel_w, panel_h, 1); cfg.gpio.r1=R1_PIN; cfg.gpio.g1=G1_PIN; cfg.gpio.b1=B1_PIN; cfg.gpio.r2=R2_PIN; cfg.gpio.g2=G2_PIN; cfg.gpio.b2=B2_PIN; cfg.gpio.a=A_PIN; cfg.gpio.b=B_PIN; cfg.gpio.c=C_PIN; cfg.gpio.d=D_PIN; cfg.gpio.lat=LAT_PIN; cfg.gpio.oe=OE_PIN; cfg.gpio.clk=CLK_PIN; display=new MatrixPanel_I2S_DMA(cfg); display->begin(); }
void closeGIFs() { if(gifL_open){gifL.close();gifL_open=false;} if(gifR_open){gifR.close();gifR_open=false;} }
void * GIFOpenFile(const char *fn, int32_t *s) { File* f = new File(FFat.open(fn, "r")); if (*f) { *s = f->size(); return (void *)f; } delete f; return NULL; }
void GIFCloseFile(void *h) { File *f = (File *)h; if(f){f->close(); delete f;} }
int32_t GIFReadFile(GIFFILE *p, uint8_t *b, int32_t l) { File *f = (File *)p->fHandle; int32_t r = f->read(b, l); p->iPos = f->position(); return r; }
int32_t GIFSeekFile(GIFFILE *p, int32_t i) { File *f = (File *)p->fHandle; f->seek(i); p->iPos = f->position(); return p->iPos; }
void GIFDraw(GIFDRAW *p) { if(!display) return; uint8_t *s = p->pPixels; uint16_t *pal = p->pPalette; int y = p->iY + p->y; int xO = ((intptr_t)p->pUser == 0) ? gifLx : gifRx; for (int x=0; x<p->iWidth; x++) { if (!p->ucHasTransparency || s[x] != p->ucTransparent) { int px = x + p->iX + xO, py = y; if(px>=0&&px<64&&py>=0&&py<32) display->drawPixel(px, py, pal[s[x]]); } } }
void GIFDrawSS(GIFDRAW *p) { if(!display) return; uint8_t *s = p->pPixels; uint16_t *pal = p->pPalette; int y = p->iY + p->y; for (int x=0; x<p->iWidth; x++) { if (!p->ucHasTransparency || s[x] != p->ucTransparent) { int px = x + p->iX + gifSSx, py = y; if(px>=0&&px<64&&py>=0&&py<32) display->drawPixel(px, py, pal[s[x]]); } } }
void draw7SegDigit(int x, int y, int n, int w, int h, uint16_t c) { if(!display) return; const uint8_t s[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F}; uint8_t seg = s[n%10]; int m = h/2, t = (segThick<1?1:segThick); if(seg&0x01) display->fillRect(x,y,w,t,c); if(seg&0x02) display->fillRect(x+w-t,y,t,m,c); if(seg&0x04) display->fillRect(x+w-t,y+m,t,m,c); if(seg&0x08) display->fillRect(x,y+h-t,w,t,c); if(seg&0x10) display->fillRect(x,y+m,t,m,c); if(seg&0x20) display->fillRect(x,y,t,m,c); if(seg&0x40) display->fillRect(x,y+m-t/2,w,t,c); }
void drawScore() { if(!display) return; display->clearScreen(); display->setBrightness8(brightness); int cx = 32, cy = 16; draw7SegDigit(cx+scoreAxOff, cy+scoreYOff, scoreA/10, segW, segH, 0xF800); draw7SegDigit(cx+scoreAxOff+segW+segSpace, cy+scoreYOff, scoreA%10, segW, segH, 0xF800); draw7SegDigit(cx+scoreBxOff, cy+scoreYOff, scoreB/10, segW, segH, 0x07E0); draw7SegDigit(cx+scoreBxOff+segW+segSpace, cy+scoreYOff, scoreB%10, segW, segH, 0x07E0); if(serveTeam=="A") display->drawFastHLine(cx+scoreAxOff, cy+scoreYOff+segH+2, segW*2+segSpace, 0xFFFF); else display->drawFastHLine(cx+scoreBxOff, cy+scoreYOff+segH+2, segW*2+segSpace, 0xFFFF); }
String safePath(String n) { return n.startsWith("/") ? n : "/" + n; }
