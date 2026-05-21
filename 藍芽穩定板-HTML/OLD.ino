#include <Preferences.h>
#include <FS.h>
#include <FFat.h>
#include <AnimatedGIF.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <WebServer.h>

// BLE 相關函式庫
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

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

/* --- 全域變數 --- */
Preferences prefs;
MatrixPanel_I2S_DMA *display = nullptr;
WebServer server(80);

bool isWifiMode = false; 
int scoreA = 0, scoreB = 0;
String serveTeam = "A"; 
bool isGameOver = false;
unsigned long lastActionTime = 0;

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

String activeGifL_path = "", activeGifR_path = "", activeGifTest_path = "";

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;
bool deviceConnected = false, oldDeviceConnected = false;

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

/* --- 函式宣告 --- */
void closeGIFs();
void * GIFOpenFile(const char *fname, int32_t *pSize);
void GIFCloseFile(void *pHandle);
int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen);
int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition);
void GIFDraw(GIFDRAW *pDraw);
void GIFDrawSS(GIFDRAW *pDraw);
void initDisplay();
void drawScore(); // 🟢 確保提前宣告，以便在設定模式中呼叫預覽

String safePath(String name) {
    if (!name.startsWith("/")) return "/" + name;
    return name;
}

/* --- 網頁 HTML --- */
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
</script></body></html>)rawliteral";

/* --- 核心邏輯 --- */
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue().c_str();
      if (rxValue.length() > 0) {
        rxValue.trim(); rxValue.toUpperCase(); 
        lastActionTime = millis(); 
        if(rxValue == "R"){ bleDoReset = true; } 
        else if(rxValue == "SETUP") { prefs.putBool("wifi_mode", true); delay(500); ESP.restart(); }
        else {
            if (rxValue == "A") { scoreA++; serveTeam="A"; }
            else if (rxValue == "B") { scoreB++; serveTeam="B"; }
            else if (rxValue == "A-") { if(scoreA > 0) scoreA--; }
            else if (rxValue == "B-") { if(scoreB > 0) scoreB--; }
            bleCheckScore = true; 
        }
        String statusMsg = "Score: A(" + String(scoreA) + ") - B(" + String(scoreB) + ")\n";
        pTxCharacteristic->setValue(statusMsg.c_str()); pTxCharacteristic->notify();
      }
    }
};

void setup() {
    Serial.begin(115200); 
    if(!FFat.begin(true)) Serial.println("FFat Mount Failed");
    
    gifL.begin(LITTLE_ENDIAN_PIXELS); gifR.begin(LITTLE_ENDIAN_PIXELS); 

    prefs.begin("scoreboard", false);
    isWifiMode = prefs.getBool("wifi_mode", false);
    
    brightness = prefs.getInt("bri", 100);
    segW = prefs.getInt("sw", 6); segH = prefs.getInt("sh", 22); 
    segThick = prefs.getInt("st", 1); segSpace = prefs.getInt("sp", 2);
    scoreAxOff = prefs.getInt("sax", -15); scoreBxOff = prefs.getInt("sbx", 2); scoreYOff = prefs.getInt("sy", -11);
    gifWin_name = prefs.getString("gw", ""); gifLose_name = prefs.getString("gls", ""); gifSS_name = prefs.getString("gss", "");
    gifLx = prefs.getInt("glx", 0); gifRx = prefs.getInt("grx", 32); gifSSx = prefs.getInt("gssx", 0);

    if (isWifiMode) {
        initDisplay();
        
        // 🟢 進入 WiFi 模式時，直接在面板畫出 00:00 的預覽
        scoreA = 0; scoreB = 0; serveTeam = "A";
        drawScore(); 

        WiFi.softAP("Badminton_Setup", "12345678");
        server.on("/", [](){ server.send(200, "text/html", setup_html); });
        
        server.on("/status", [](){
            String json = "{\"bri\":"+String(brightness)+",\"gw\":\""+gifWin_name+"\",\"gls\":\""+gifLose_name+"\",\"gss\":\""+gifSS_name+"\",\"sw\":"+String(segW)+",\"sh\":"+String(segH)+",\"st\":"+String(segThick)+",\"sp\":"+String(segSpace)+",\"sax\":"+String(scoreAxOff)+",\"sbx\":"+String(scoreBxOff)+",\"sy\":"+String(scoreYOff)+",\"glx\":"+String(gifLx)+",\"grx\":"+String(gifRx)+",\"gssx\":"+String(gifSSx)+"}";
            server.send(200, "application/json", json);
        });

        server.on("/listGifs", [](){
            String json = "["; File root = FFat.open("/"); File f = root.openNextFile(); bool first=true;
            while(f){ 
                String n = f.name(); if(n.startsWith("/")) n = n.substring(1); 
                if(n.endsWith(".gif") || n.endsWith(".GIF")){ if(!first) json+=","; json+="\""+n+"\""; first=false; } 
                f=root.openNextFile();
            }
            json+="]"; server.send(200, "application/json", json);
        });

        server.on("/getGif", [](){
            String name = safePath(server.arg("name"));
            if(FFat.exists(name)) { File f = FFat.open(name, "r"); server.streamFile(f, "image/gif"); f.close(); } 
            else server.send(404);
        });

        server.on("/rename", [](){
            String oldN = safePath(server.arg("old")), newN = safePath(server.arg("new"));
            if(FFat.rename(oldN, newN)) server.send(200, "text/plain", "更名成功");
            else server.send(200, "text/plain", "更名失敗");
        });

        server.on("/delete", [](){ FFat.remove(safePath(server.arg("name"))); server.send(200); });

        server.on("/test", [](){
            String name = safePath(server.arg("name"));
            closeGIFs(); display->clearScreen();
            
            if (gifL.open(name.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDrawSS)) {
                gifL_open = true;
                activeGifTest_path = name; 
                testEndTime = millis() + 5000;
                nextFrameL = millis();
                server.send(200);
            } else {
                server.send(500, "text/plain", "檔案無法解碼 (可能過大或毀損，請用 ezgif 重轉)");
            }
        });

        server.on("/config", [](){
            // 🟢 即時將網頁傳來的變數，同步寫入記憶體「且」更新到現在運作中的 RAM 變數
            if(server.hasArg("gw")) { gifWin_name = server.arg("gw"); prefs.putString("gw", gifWin_name); }
            if(server.hasArg("gls")) { gifLose_name = server.arg("gls"); prefs.putString("gls", gifLose_name); }
            if(server.hasArg("gss")) { gifSS_name = server.arg("gss"); prefs.putString("gss", gifSS_name); }
            
            if(server.hasArg("b")) { brightness = server.arg("b").toInt(); prefs.putInt("bri", brightness); }
            if(server.hasArg("sw")) { segW = server.arg("sw").toInt(); prefs.putInt("sw", segW); }
            if(server.hasArg("sh")) { segH = server.arg("sh").toInt(); prefs.putInt("sh", segH); }
            if(server.hasArg("st")) { segThick = server.arg("st").toInt(); prefs.putInt("st", segThick); }
            if(server.hasArg("sp")) { segSpace = server.arg("sp").toInt(); prefs.putInt("sp", segSpace); }
            if(server.hasArg("sax")) { scoreAxOff = server.arg("sax").toInt(); prefs.putInt("sax", scoreAxOff); }
            if(server.hasArg("sbx")) { scoreBxOff = server.arg("sbx").toInt(); prefs.putInt("sbx", scoreBxOff); }
            if(server.hasArg("sy")) { scoreYOff = server.arg("sy").toInt(); prefs.putInt("sy", scoreYOff); }
            if(server.hasArg("glx")) { gifLx = server.arg("glx").toInt(); prefs.putInt("glx", gifLx); }
            if(server.hasArg("grx")) { gifRx = server.arg("grx").toInt(); prefs.putInt("grx", gifRx); }
            if(server.hasArg("gssx")) { gifSSx = server.arg("gssx").toInt(); prefs.putInt("gssx", gifSSx); }

            // 🟢 即時重繪！如果目前沒有在播放 GIF 測試，就立刻在 LED 上畫出調整後的分數版面
            if (!gifL_open) {
                scoreA = 0; scoreB = 0;
                drawScore();
            }
            
            server.send(200);
        });

        server.on("/exit", [](){ prefs.putBool("wifi_mode", false); server.send(200); delay(500); ESP.restart(); });
        
        static File fsUploadFile;
        server.on("/upload", HTTP_POST, [](){ server.send(200); }, [](){
            HTTPUpload& upload = server.upload();
            if(upload.status == UPLOAD_FILE_START) { fsUploadFile = FFat.open(safePath(upload.filename), FILE_WRITE); }
            else if(upload.status == UPLOAD_FILE_WRITE) { if(fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize); }
            else if(upload.status == UPLOAD_FILE_END) { if(fsUploadFile) fsUploadFile.close(); }
        });
        server.begin();
    } else {
        initDisplay();
        BLEDevice::init("Badminton_Score_BLE"); 
        pServer = BLEDevice::createServer();
        BLEService *pS = pServer->createService(SERVICE_UUID);
        pTxCharacteristic = pS->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
        BLECharacteristic *pRx = pS->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
        pRx->setCallbacks(new MyCallbacks());
        pS->start(); pServer->getAdvertising()->start();
        lastActionTime = millis();
    }
}

void loop() {
    unsigned long now = millis();

    if (isWifiMode) {
        server.handleClient();
        if (gifL_open && now < testEndTime) {
            if (now >= nextFrameL) {
                int d = 0; 
                if (gifL.playFrame(false, &d, NULL)) {
                    if (d < 20) d = 100;
                    nextFrameL = now + d;
                } else {
                    gifL.close();
                    gifL.open(activeGifTest_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDrawSS);
                    nextFrameL = now + 50; 
                }
            }
        } else if (gifL_open) { 
            // 🟢 當 5 秒的 GIF 測試播完時，自動清空螢幕，並呼叫 drawScore 恢復 00:00 預覽
            closeGIFs(); 
            scoreA = 0; scoreB = 0;
            drawScore(); 
        }
        return;
    }
    
    if (bleDoReset) { scoreA=0; scoreB=0; isGameOver=false; inScreenSaver=false; closeGIFs(); display->clearScreen(); needsUpdateUI=true; bleDoReset=false; }
    
    if (bleCheckScore) {
        bool currentGameOver = ((scoreA>=21||scoreB>=21)&&(abs(scoreA-scoreB)>=2||scoreA==30||scoreB==30));
        if (currentGameOver && !isGameOver) {
            isGameOver=true; display->clearScreen();
            activeGifL_path = (scoreA > scoreB) ? safePath(gifWin_name) : safePath(gifLose_name);
            activeGifR_path = (scoreB > scoreA) ? safePath(gifWin_name) : safePath(gifLose_name);
            
            if(activeGifL_path != "/") { gifL_open = gifL.open(activeGifL_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw); nextFrameL = now; }
            if(activeGifR_path != "/") { gifR_open = gifR.open(activeGifR_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw); nextFrameR = now; }
        } else if (!currentGameOver && isGameOver) { isGameOver=false; closeGIFs(); display->clearScreen(); }
        needsUpdateUI=true; bleCheckScore=false;
    }

    if (now - lastActionTime > 180000 && !isGameOver) {
        if (!inScreenSaver) {
            inScreenSaver = true; closeGIFs(); display->clearScreen();
            activeGifTest_path = safePath(gifSS_name); 
            if (activeGifTest_path != "/") { 
                gifL_open = gifL.open(activeGifTest_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDrawSS); 
                nextFrameL = now; 
            }
        }
        if (gifL_open && now >= nextFrameL) { 
            int d=0; 
            if (gifL.playFrame(false, &d, NULL)) { 
                if(d < 20) d = 100; nextFrameL = now + d; 
            } else { 
                gifL.close(); gifL.open(activeGifTest_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDrawSS);
                nextFrameL = now + 50; 
            } 
        }
    } else {
        if (inScreenSaver) { inScreenSaver = false; closeGIFs(); display->clearScreen(); needsUpdateUI = true; }
        
        if (isGameOver) {
            if(gifL_open && now>=nextFrameL){ 
                int d=0; 
                if(gifL.playFrame(false,&d,(void*)0)) { if(d < 20) d = 100; nextFrameL=now+d; } 
                else { gifL.close(); gifL.open(activeGifL_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw); nextFrameL=now+50; } 
            }
            if(gifR_open && now>=nextFrameR){ 
                int d=0; 
                if(gifR.playFrame(false,&d,(void*)1)) { if(d < 20) d = 100; nextFrameR=now+d; } 
                else { gifR.close(); gifR.open(activeGifR_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw); nextFrameR=now+50; } 
            }
        } else if(needsUpdateUI) { drawScore(); needsUpdateUI=false; }
    }
}

/* --- 繪圖工具與顯示初始化 --- */
void initDisplay() {
    HUB75_I2S_CFG cfg(64, 32, 1);
    cfg.gpio.r1=R1_PIN; cfg.gpio.g1=G1_PIN; cfg.gpio.b1=B1_PIN; cfg.gpio.r2=R2_PIN; cfg.gpio.g2=G2_PIN; cfg.gpio.b2=B2_PIN;
    cfg.gpio.a=A_PIN; cfg.gpio.b=B_PIN; cfg.gpio.c=C_PIN; cfg.gpio.d=D_PIN; cfg.gpio.lat=LAT_PIN; cfg.gpio.oe=OE_PIN; cfg.gpio.clk=CLK_PIN;
    display = new MatrixPanel_I2S_DMA(cfg); display->begin();
}

void closeGIFs() { 
    if(gifL_open) { gifL.close(); gifL_open = false; } 
    if(gifR_open) { gifR.close(); gifR_open = false; } 
}

void * GIFOpenFile(const char *fname, int32_t *pSize) { File* f = new File(FFat.open(fname, "r")); if (*f) { *pSize = f->size(); return (void *)f; } delete f; return NULL; }
void GIFCloseFile(void *pHandle) { File *f = (File *)pHandle; if (f) { f->close(); delete f; } }
int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) { File *f = (File *)pFile->fHandle; int32_t b = f->read(pBuf, iLen); pFile->iPos = f->position(); return b; }
int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPos) { File *f = (File *)pFile->fHandle; f->seek(iPos); pFile->iPos = f->position(); return pFile->iPos; }

void drawPixelSafe(int x, int y, uint16_t color) {
    if (x >= 0 && x < panel_w && y >= 0 && y < panel_h) display->drawPixel(x, y, color);
}

void GIFDraw(GIFDRAW *pDraw) {
    uint8_t *s = pDraw->pPixels; uint16_t *pal = pDraw->pPalette; int y = pDraw->iY + pDraw->y;
    int xOff = ((intptr_t)pDraw->pUser == 0) ? gifLx : gifRx;
    for (int x = 0; x < pDraw->iWidth; x++) { 
        if (pDraw->ucHasTransparency && s[x] == pDraw->ucTransparent) continue; 
        drawPixelSafe(x + pDraw->iX + xOff, y, pal[s[x]]); 
    }
}

void GIFDrawSS(GIFDRAW *pDraw) {
    uint8_t *s = pDraw->pPixels; uint16_t *pal = pDraw->pPalette; int y = pDraw->iY + pDraw->y;
    for (int x = 0; x < pDraw->iWidth; x++) { 
        if (pDraw->ucHasTransparency && s[x] == pDraw->ucTransparent) continue; 
        drawPixelSafe(x + pDraw->iX + gifSSx, y, pal[s[x]]); 
    }
}

void drawWrappedHLine(int x, int y, int w, uint16_t color) { for (int i = 0; i < w; i++) drawPixelSafe(x + i, y, color); }
void drawWrappedVLine(int x, int y, int h, uint16_t color) { for (int i = 0; i < h; i++) drawPixelSafe(x, y + i, color); }
void drawWrappedFillRect(int x, int y, int w, int h, uint16_t color) { for (int i = 0; i < w; i++) drawWrappedVLine(x + i, y, h, color); }
void draw7SegDigit(int x, int y, int num, int w, int h, uint16_t color) {
    const uint8_t segments[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F }; uint8_t seg = segments[num % 10]; int mid = h / 2, sw = segThick;
    if (seg & 0x01) drawWrappedFillRect(x, y, w, sw, color); if (seg & 0x02) drawWrappedFillRect(x + w - sw, y, sw, mid, color);
    if (seg & 0x04) drawWrappedFillRect(x + w - sw, y + mid, sw, mid, color); if (seg & 0x08) drawWrappedFillRect(x, y + h - sw, w, sw, color);
    if (seg & 0x10) drawWrappedFillRect(x, y + mid, sw, mid, color); if (seg & 0x20) drawWrappedFillRect(x, y, sw, mid, color);
    if (seg & 0x40) drawWrappedFillRect(x, y + mid - (sw / 2), w, sw, color);
    if (sw >= 2) { drawPixelSafe(x, y, 0x0000); drawPixelSafe(x + w - 1, y, 0x0000); drawPixelSafe(x, y + h - 1, 0x0000); drawPixelSafe(x + w - 1, y + h - 1, 0x0000); }
}

void drawScore() {
    display->clearScreen(); display->setBrightness8(brightness); int cx = panel_w / 2, cy = panel_h / 2;
    draw7SegDigit(cx + scoreAxOff, cy + scoreYOff, scoreA/10, segW, segH, 0xF800); draw7SegDigit(cx + scoreAxOff + segW + segSpace, cy + scoreYOff, scoreA%10, segW, segH, 0xF800);
    draw7SegDigit(cx + scoreBxOff, cy + scoreYOff, scoreB/10, segW, segH, 0x07E0); draw7SegDigit(cx + scoreBxOff + segW + segSpace, cy + scoreYOff, scoreB%10, segW, segH, 0x07E0);
    int lineW = segW * 2 + segSpace; int lineY = cy + scoreYOff + segH + 2; 
    if (serveTeam == "A") drawWrappedHLine(cx + scoreAxOff, lineY, lineW, 0xFFFF); else drawWrappedHLine(cx + scoreBxOff, lineY, lineW, 0xFFFF); 
}