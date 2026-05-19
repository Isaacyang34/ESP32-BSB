/* ========================================================
 * 羽球記分板全功能韌體 (基於 BS.ino 穩定版)
 * ========================================================
 * 修正與整合項目：
 * 1. 採用課長確認過的最穩定版 (BS.ino) 作為基底：
 *    - 包含 FFat 檔案系統、WebServer、BLE 斷線重啟。
 *    - 語音採用 ScoreState 狀態機精確控制 (VOICE_GAP_MS 等參數)。
 *    - 完美的 HUB75 腳位與顯示驅動配置。
 * 2. 補回因檔案截斷遺失的 WiFi 設定網頁 API (config, rename, getGif 等)。
 * 3. 整合最近新增的「殺球 (SMASH)」與「界外 (OUT)」單次動畫播放邏輯。
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

/* --- 📋 課長專屬調校參數區 (已改為動態變數，可由 WiFi 網頁調整) --- */
int   voiceVolume      = 25;    // 固定開機音量 (0~30)
int   voiceGapMs       = 1000;  // [分數] 到 [比]、[比] 到 [下個分數] 的間隔 (ms)
int   voiceStopDelayMs = 1500;  // 最後一段分報完後，隔多久才送 Stop 指令 (ms)
int   voiceMode        = 0x00;  // 播放模式：0x00 通常為單曲停止
int   debounceMs       = 450;   // 藍牙指令去彈跳時間 (ms)

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
String gifSmash_name = "", gifOut_name = "";            // ✅ 新增：殺球與界外動畫名稱
int gifLx = 0, gifRx = 32, gifSSx = 0;                  
unsigned long nextFrameL = 0, nextFrameR = 0;
unsigned long testEndTime = 0;
bool needsUpdateUI = true;
bool wifiUiDrawn = false;  // 防止 WiFi 模式每 loop 重繪螢幕
unsigned long wifiStartTime = 0; // WiFi 模式啟動時間，用於超時保護

bool bleDoReset = false, bleCheckScore = false, inScreenSaver = false;
bool playEventGif = false;                              // ✅ 新增：單次事件播放旗標
String eventGifName = "";                               // ✅ 新增：單次事件檔名
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
    byte cmd[] = {0x7E, 0x0A, 0xFF, 0xFF, 0x18, (byte)voiceMode, 0x00, 0x00, 0x00, 0xEF};
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
            nextVoiceActionTime = now + voiceGapMs;
            audioState = STATE_WAIT_GAP_1;
            break;
        case STATE_WAIT_GAP_1:
            audioState = STATE_PLAY_BI;
            break;
        case STATE_PLAY_BI:
            sendPlayCommand(32);
            nextVoiceActionTime = now + voiceGapMs;
            audioState = STATE_WAIT_GAP_2;
            break;
        case STATE_WAIT_GAP_2:
            audioState = STATE_PLAY_B;
            break;
        case STATE_PLAY_B:
            sendPlayCommand(targetScoreSecond + 1);
            nextVoiceActionTime = now + voiceStopDelayMs;
            audioState = STATE_WAIT_STOP_CMD;
            break;
        case STATE_WAIT_STOP_CMD:
            sendStopCommand();
            audioState = STATE_WAITING;
            break;
        default: break;
    }
}

/* --- 網頁 HTML 介面 (整合了殺球與界外設定) --- */
const char setup_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
<style>
  body{background:#121212;color:#eee;font-family:sans-serif;text-align:center;margin:0;padding:env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left);}
  .card{background:#2a2a2a;margin-bottom:12px;padding:15px;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,.5);text-align:left;}
  button{padding:10px;border-radius:8px;border:none;cursor:pointer;font-weight:bold;touch-action:manipulation;}button:active{transform:scale(0.93);}
  .btn-sys{background:#e53935;color:white;width:100%;font-size:1.2rem;padding:15px;margin-bottom:10px;}
  .ctrl-row{display:flex;align-items:center;justify-content:space-between;margin:8px 0;background:#1e1e1e;padding:10px;border-radius:8px;}
  .preview-box{width:256px;height:128px;background:#000;border:2px solid #444;margin:10px auto;display:flex;align-items:center;justify-content:center;overflow:hidden;}
  .preview-box img{width:100%;height:100%;image-rendering:pixelated;object-fit:contain;}
  .btn-test{background:#28a745;color:white;width:100%;padding:15px;font-size:1.1rem;margin:10px 0;}
  input[type=text],select{padding:10px;background:#444;color:white;border:none;border-radius:5px;width:100%;box-sizing:border-box;}
  input[type=number]{padding:8px;background:#000;color:#0ff;border:1px solid #444;border-radius:5px;width:80px;text-align:center;font-family:monospace;font-size:1.2rem;}
  .tab-bar{display:flex;margin-bottom:12px;border-radius:12px;overflow:hidden;border:1px solid #333;}
  .tab-btn{flex:1;padding:14px 0;border:none;cursor:pointer;font-size:1.1rem;font-weight:bold;background:#2a2a2a;color:#888;transition:.2s;}
  .tab-btn.on{background:#007bff;color:#fff;}
  .tab-pg{display:none;}.tab-pg.on{display:block;}
  .sbd{background:#1a1a1a;border-radius:16px;padding:20px 10px;margin-bottom:12px;}
  .snum{font-size:5rem;font-weight:bold;width:90px;text-align:center;font-family:monospace;line-height:1;}
  .sdot{display:inline-block;width:10px;height:10px;border-radius:50%;background:#444;margin-bottom:4px;}
  .sdot.on{background:#fff;box-shadow:0 0 8px #fff;}
  .bsc{width:44%;font-size:2rem;color:white;padding:18px 0;border-radius:12px;margin:4px 0;}
  .bsub{width:44%;font-size:1.3rem;padding:13px 0;border-radius:10px;color:#ccc;margin:4px 0;}
  .bev{width:44%;font-size:1.1rem;padding:13px 0;border-radius:10px;margin:4px 0;}
</style></head><body>
<div class="tab-bar">
  <button class="tab-btn on" id="tb-sc" onclick="tab('sc')">🏸 計分控制</button>
  <button class="tab-btn" id="tb-cfg" onclick="tab('cfg')">⚙️ 設定</button>
</div>
<div id="pg-sc" class="tab-pg on">
  <div class="sbd">
    <div style="display:flex;justify-content:center;align-items:center;gap:20px;">
      <div><div class="sdot" id="da"></div><div class="snum" id="sa" style="color:#ff4444">0</div><div style="font-size:.85rem;color:#f66">A 隊</div></div>
      <div style="font-size:3rem;color:#555">:</div>
      <div><div class="sdot" id="db"></div><div class="snum" id="sb" style="color:#44ff44">0</div><div style="font-size:.85rem;color:#6f6">B 隊</div></div>
    </div>
    <div id="gst" style="font-size:1rem;color:#ffc107;min-height:22px;margin-top:8px;"></div>
  </div>
  <div style="display:flex;justify-content:space-around;margin-bottom:6px;">
    <button class="bsc" style="background:#c0392b" onclick="ctrl('A')">A +1</button>
    <button class="bsc" style="background:#27ae60" onclick="ctrl('B')">B +1</button>
  </div>
  <div style="display:flex;justify-content:space-around;margin-bottom:6px;">
    <button class="bsub" style="background:#7f1e14" onclick="ctrl('A-')">A -1</button>
    <button class="bsub" style="background:#145e2c" onclick="ctrl('B-')">B -1</button>
  </div>
  <div style="display:flex;justify-content:space-around;margin-bottom:6px;">
    <button class="bev" style="background:#d39e00;color:#000" onclick="ctrl('SMASH')">💥 殺球</button>
    <button class="bev" style="background:#17a2b8;color:#fff" onclick="ctrl('OUT')">❌ 界外</button>
  </div>
  <button style="width:93%;padding:14px;font-size:1.1rem;border-radius:10px;background:#555;color:white;margin:4px 0;" onclick="ctrl('R')">🔄 重設分數</button>
  <div id="cst" style="font-size:.85rem;color:#888;padding:8px;min-height:24px;">等待操作...</div>
</div>
<div id="pg-cfg" class="tab-pg">
  <button class="btn-sys" onclick="exitSetup()">💾 儲存並重啟回到藍牙模式</button>
  
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
    <div style="margin-bottom:10px;">💥 殺球: <select id="gsm" onchange="conf()"></select></div>
    <div style="margin-bottom:10px;">❌ 界外: <select id="gout" onchange="conf()"></select></div>
    <hr>
    <div class="ctrl-row"><span>左方動畫 X 偏移:</span><input type="number" id="glx" onchange="conf()"></div>
    <div class="ctrl-row"><span>右方動畫 X 偏移:</span><input type="number" id="grx" onchange="conf()"></div>
    <div class="ctrl-row" style="border:1px solid #17a2b8;"><span>待機動畫 X 偏移:</span><input type="number" id="gssx" onchange="conf()"></div>
  </div>

  <div class="card">
    <h3 style="color:#ff9800;">🔊 語音報分參數</h3>
    <div class="ctrl-row"><span>音量 (0~30):</span><input type="number" id="v_vol" min="0" max="30" onchange="confVoice()"></div>
    <div class="ctrl-row"><span>分數間隔 (ms):</span><input type="number" id="v_gap" min="100" max="5000" step="100" onchange="confVoice()"></div>
    <div class="ctrl-row"><span>停止延遲 (ms):</span><input type="number" id="v_stop" min="100" max="5000" step="100" onchange="confVoice()"></div>
    <div class="ctrl-row"><span>播放模式 (hex):</span><input type="number" id="v_mode" min="0" max="255" onchange="confVoice()"></div>
    <div class="ctrl-row"><span>去彈跳時間 (ms):</span><input type="number" id="v_dbnc" min="50" max="2000" step="50" onchange="confVoice()"></div>
    <div style="font-size:0.8rem;color:#888;padding:6px 4px;">⚠️ 調整後立即生效並儲存，重啟後繼續使用</div>
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
</div>
<script>
function tab(id){
  ['sc','cfg'].forEach(t=>{document.getElementById('pg-'+t).classList.remove('on');document.getElementById('tb-'+t).classList.remove('on');});
  document.getElementById('pg-'+id).classList.add('on');document.getElementById('tb-'+id).classList.add('on');
}
function ctrl(cmd){
  document.getElementById('cst').innerText='⏳ 發送中...';
  fetch('/ctrl?cmd='+cmd).then(r=>r.json()).then(d=>{updSc(d);document.getElementById('cst').innerText='✅ '+cmd+' 已執行';}).catch(()=>{document.getElementById('cst').innerText='❌ 連線失敗';});
}
function updSc(d){
  document.getElementById('sa').innerText=d.a;document.getElementById('sb').innerText=d.b;
  document.getElementById('da').className='sdot'+(d.serve==='A'?' on':'');
  document.getElementById('db').className='sdot'+(d.serve==='B'?' on':'');
  document.getElementById('gst').innerText=d.over?(parseInt(d.a)>parseInt(d.b)?'🏆 A 隊勝利！':'🏆 B 隊勝利！'):'';
}
setInterval(()=>{if(document.getElementById('pg-sc').classList.contains('on'))fetch('/ctrl?cmd=STATUS').then(r=>r.json()).then(updSc).catch(()=>{});},3000);
window.onload = function() {
  fetch('/ctrl?cmd=STATUS').then(r=>r.json()).then(updSc).catch(()=>{});
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('bri').value = d.bri;
    const keys = ['sw','sh','st','sp','sax','sbx','sy','glx','grx','gssx'];
    keys.forEach(k => { if(document.getElementById(k)) document.getElementById(k).value = d[k]; });
    const vkeys = ['v_vol','v_gap','v_stop','v_mode','v_dbnc'];
    vkeys.forEach(k => { if(document.getElementById(k)) document.getElementById(k).value = d[k]; });
    refreshFileList(d.gw, d.gls, d.gss, d.gsm, d.gout);
  });
};

function refreshFileList(gw, gls, gss, gsm, gout) {
  fetch('/listGifs').then(r=>r.json()).then(arr => {
    let opts = '<option value="">-- 選擇檔案 --</option>';
    arr.forEach(f => opts += `<option value="${f}">${f}</option>`);
    ['fileSel','gw','gls','gss','gsm','gout'].forEach(id => {
      const el = document.getElementById(id);
      if(el) el.innerHTML = opts;
    });
    if(arr.includes(gw)) document.getElementById('gw').value = gw; 
    if(arr.includes(gls)) document.getElementById('gls').value = gls; 
    if(arr.includes(gss)) document.getElementById('gss').value = gss;
    if(arr.includes(gsm)) document.getElementById('gsm').value = gsm;
    if(arr.includes(gout)) document.getElementById('gout').value = gout;
  });
}

function updatePreview() {
  const f = document.getElementById('fileSel').value;
  document.getElementById('preImg').src = f ? `/getGif?name=${f}` : '';
}

function renameFile() {
  const oldN = document.getElementById('fileSel').value;
  let newN = document.getElementById('newName').value.trim();
  if(!oldN || !newN) return alert("請選檔案並輸入新名稱");
  if(!newN.toLowerCase().endsWith('.gif')) newN += '.gif'; // 自動補副檔名
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
  let q = `/config?b=${document.getElementById('bri').value}&gw=${document.getElementById('gw').value}&gls=${document.getElementById('gls').value}&gss=${document.getElementById('gss').value}&gsm=${document.getElementById('gsm').value}&gout=${document.getElementById('gout').value}`;
  keys.forEach(k => { q += `&${k}=${document.getElementById(k).value}`; });
  fetch(q);
}

function confVoice(){
  const vkeys = ['v_vol','v_gap','v_stop','v_mode','v_dbnc'];
  let q = '/config?';
  vkeys.forEach((k,i) => { q += (i?'&':'') + k + '=' + document.getElementById(k).value; });
  fetch(q);
}

function uploadGif() {
  const file = document.getElementById('gifFile').files[0]; if(!file) return alert('請選擇檔案');
  const fd = new FormData(); fd.append('file', file); document.getElementById('upStatus').innerText = '⏳ 上傳中...';
  fetch('/upload', {method: 'POST', body: fd}).then(r => { if(r.ok) { alert('上傳成功'); location.reload(); } });
}

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

/* --- BLE 數據回調：處理按鍵與新語音指令 --- */
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pC) {
        std::string rawValue = pC->getValue();
        unsigned long now = millis();
        if (rawValue.length() == 0 || (now - lastBleCommandTime < (unsigned long)debounceMs)) return;
        
        String rxValue = String(rawValue.c_str()); rxValue.trim(); rxValue.toUpperCase();
        lastBleCommandTime = now; lastActionTime = now;
        
        if(rxValue == "R") { bleDoReset = true; } 
        else if(rxValue == "SETUP") { prefs.putBool("wifi_mode", true); delay(500); ESP.restart(); }
        else if(rxValue == "SMASH") { 
            // ✅ 收到殺球事件
            if (gifSmash_name != "") { eventGifName = safePath(gifSmash_name); playEventGif = true; }
        }
        else if(rxValue == "OUT") { 
            // ✅ 收到界外事件
            if (gifOut_name != "") { eventGifName = safePath(gifOut_name); playEventGif = true; }
        }
        else {
            // ✅ 計分邏輯
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
    
    if(!FFat.begin(true)) Serial.println("[ERR] FFat 失敗");
    gifL.begin(LITTLE_ENDIAN_PIXELS);
    gifR.begin(LITTLE_ENDIAN_PIXELS);

    prefs.begin("scoreboard", false);
    isWifiMode = prefs.getBool("wifi_mode", false);
    brightness = prefs.getInt("bri", 100);
    segW = prefs.getInt("sw", 12); segH = prefs.getInt("sh", 24);
    segThick = prefs.getInt("st", 3); segSpace = prefs.getInt("sp", 2);
    scoreAxOff = prefs.getInt("sax", -28); scoreBxOff = prefs.getInt("sbx", 4); scoreYOff = prefs.getInt("sy", -12);
    gifWin_name = prefs.getString("gw", ""); gifLose_name = prefs.getString("gls", ""); gifSS_name = prefs.getString("gss", "");
    gifSmash_name = prefs.getString("gsm", ""); gifOut_name = prefs.getString("gout", "");
    gifLx = prefs.getInt("glx", 0); gifRx = prefs.getInt("grx", 32); gifSSx = prefs.getInt("gssx", 0);
    // 語音報分參數（從 Preferences 讀取，支援 WiFi 網頁調整）
    voiceVolume      = prefs.getInt("v_vol",  25);
    voiceGapMs       = prefs.getInt("v_gap",  1000);
    voiceStopDelayMs = prefs.getInt("v_stop", 1500);
    voiceMode        = prefs.getInt("v_mode", 0);
    debounceMs       = prefs.getInt("v_dbnc", 450);

    if (isWifiMode) {
        // WiFi 模式：先啟動 WiFi AP（在 I2S DMA 之前），再初始化顯示
        WiFi.softAP("Badminton_Setup", "12345678");
        Serial.printf("[WiFi] IP: %s\n", WiFi.softAPIP().toString().c_str());

        initDisplay();
        scoreA = 0; scoreB = 0; serveTeam = "A";
        drawScore();

        server.on("/", [](){ server.send(200, "text/html", setup_html); });
        
        server.on("/status", [](){
            String j = "{";
            j += "\"bri\":" + String(brightness);
            j += ",\"gw\":\"" + gifWin_name + "\"";
            j += ",\"gls\":\"" + gifLose_name + "\"";
            j += ",\"gss\":\"" + gifSS_name + "\"";
            j += ",\"gsm\":\"" + gifSmash_name + "\"";
            j += ",\"gout\":\"" + gifOut_name + "\"";
            j += ",\"sw\":" + String(segW);
            j += ",\"sh\":" + String(segH);
            j += ",\"st\":" + String(segThick);
            j += ",\"sp\":" + String(segSpace);
            j += ",\"sax\":" + String(scoreAxOff);
            j += ",\"sbx\":" + String(scoreBxOff);
            j += ",\"sy\":" + String(scoreYOff);
            j += ",\"glx\":" + String(gifLx);
            j += ",\"grx\":" + String(gifRx);
            j += ",\"gssx\":" + String(gifSSx);
            // 語音參數
            j += ",\"v_vol\":" + String(voiceVolume);
            j += ",\"v_gap\":" + String(voiceGapMs);
            j += ",\"v_stop\":" + String(voiceStopDelayMs);
            j += ",\"v_mode\":" + String(voiceMode);
            j += ",\"v_dbnc\":" + String(debounceMs);
            j += "}";
            server.send(200, "application/json", j);
        });
        
        // ✅ 補回遺失的 API 介面 (config, rename, delete, test, getGif)
        server.on("/config", [](){
            if(server.hasArg("gw")) { gifWin_name = server.arg("gw"); prefs.putString("gw", gifWin_name); }
            if(server.hasArg("gls")) { gifLose_name = server.arg("gls"); prefs.putString("gls", gifLose_name); }
            if(server.hasArg("gss")) { gifSS_name = server.arg("gss"); prefs.putString("gss", gifSS_name); }
            if(server.hasArg("gsm")) { gifSmash_name = server.arg("gsm"); prefs.putString("gsm", gifSmash_name); }
            if(server.hasArg("gout")) { gifOut_name = server.arg("gout"); prefs.putString("gout", gifOut_name); }
            
            if(server.hasArg("b")) { brightness = server.arg("b").toInt(); prefs.putInt("bri", brightness); if(display) display->setBrightness8(brightness); }
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
            // 語音參數
            if(server.hasArg("v_vol"))  { voiceVolume      = server.arg("v_vol").toInt();  prefs.putInt("v_vol",  voiceVolume);      setVolume((byte)voiceVolume); }
            if(server.hasArg("v_gap"))  { voiceGapMs       = server.arg("v_gap").toInt();  prefs.putInt("v_gap",  voiceGapMs); }
            if(server.hasArg("v_stop")) { voiceStopDelayMs = server.arg("v_stop").toInt(); prefs.putInt("v_stop", voiceStopDelayMs); }
            if(server.hasArg("v_mode")) { voiceMode        = server.arg("v_mode").toInt(); prefs.putInt("v_mode", voiceMode); }
            if(server.hasArg("v_dbnc")) { debounceMs       = server.arg("v_dbnc").toInt(); prefs.putInt("v_dbnc", debounceMs); }

            if (!gifL_open) { scoreA = 0; scoreB = 0; drawScore(); }
            server.send(200);
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
            closeGIFs(); if(display) display->clearScreen();
            if (gifL.open(name.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDrawSS)) {
                gifL_open = true; activeGifTest_path = name; testEndTime = millis() + 5000; nextFrameL = millis();
                server.send(200);
            } else { server.send(500, "text/plain", "播放失敗"); }
        });

        server.on("/listGifs", [](){ 
            String json = "["; 
            File root = FFat.open("/"); 
            File f = root.openNextFile(); 
            bool first=true;
            while(f){ 
                String n = f.name(); 
                if(n.startsWith("/")) n = n.substring(1); // 移除前置斜線，避免 URL 參數解析問題
                if(n.endsWith(".gif") || n.endsWith(".GIF")){ 
                    if(!first) json+=","; 
                    json+="\""+n+"\""; 
                    first=false; 
                } 
                f=root.openNextFile();
            }
            json+="]"; 
            server.send(200, "application/json", json);
        });

        server.on("/upload", HTTP_POST, [](){ server.send(200); }, [](){ HTTPUpload& u = server.upload(); static File f; if(u.status == UPLOAD_FILE_START) f = FFat.open(safePath(u.filename), FILE_WRITE); else if(u.status == UPLOAD_FILE_WRITE) f.write(u.buf, u.currentSize); else if(u.status == UPLOAD_FILE_END) f.close(); });

        // ✅ WiFi 計分控制 API
        server.on("/ctrl", [](){
            String cmd = server.arg("cmd");
            if (cmd != "STATUS") {
                if      (cmd == "A")     { scoreA++; serveTeam="A"; needsUpdateUI=true; wifiUiDrawn=false; }
                else if (cmd == "B")     { scoreB++; serveTeam="B"; needsUpdateUI=true; wifiUiDrawn=false; }
                else if (cmd == "A-")   { if(scoreA>0) scoreA--; needsUpdateUI=true; wifiUiDrawn=false; }
                else if (cmd == "B-")   { if(scoreB>0) scoreB--; needsUpdateUI=true; wifiUiDrawn=false; }
                else if (cmd == "R")    { scoreA=0; scoreB=0; serveTeam="A"; isGameOver=false; playEventGif=false; closeGIFs(); if(display) display->clearScreen(); needsUpdateUI=true; wifiUiDrawn=false; }
                else if (cmd == "SMASH"){ if(gifSmash_name!=""){eventGifName=safePath(gifSmash_name);playEventGif=true;} }
                else if (cmd == "OUT")  { if(gifOut_name!=""){eventGifName=safePath(gifOut_name);playEventGif=true;} }
                // 勝負判斷
                bool go=((scoreA>=21||scoreB>=21)&&(abs(scoreA-scoreB)>=2||scoreA==30||scoreB==30));
                if(go&&!isGameOver){
                    isGameOver=true; playEventGif=false; closeGIFs(); if(display) display->clearScreen();
                    activeGifL_path=safePath((scoreA>scoreB)?gifWin_name:gifLose_name);
                    activeGifR_path=safePath((scoreB>scoreA)?gifWin_name:gifLose_name);
                    gifL_open=gifL.open(activeGifL_path.c_str(),GIFOpenFile,GIFCloseFile,GIFReadFile,GIFSeekFile,GIFDraw);
                    gifR_open=gifR.open(activeGifR_path.c_str(),GIFOpenFile,GIFCloseFile,GIFReadFile,GIFSeekFile,GIFDraw);
                    nextFrameL=millis(); nextFrameR=millis(); needsUpdateUI=false;
                } else if(!go&&isGameOver){ isGameOver=false; closeGIFs(); if(display) display->clearScreen(); needsUpdateUI=true; wifiUiDrawn=false; }
            }
            String j="{\"a\":"+String(scoreA)+",\"b\":"+String(scoreB)+",\"serve\":\""+serveTeam+"\",\"over\":"+String(isGameOver?"true":"false")+"}";
            server.send(200, "application/json", j);
        });

        server.on("/exit", [](){ prefs.putBool("wifi_mode", false); server.send(200, "text/plain", "Restarting..."); delay(1000); ESP.restart(); });
        server.begin();
    } else {
        AudioSerial.begin(9600, SERIAL_8N1, PIN_AUDIO_RX, PIN_AUDIO_TX);
        while(AudioSerial.available()) AudioSerial.read(); AudioSerial.flush();
        setVolume((byte)voiceVolume); delay(150);
        setSingleMode(); delay(150);
        sendStopCommand();

        initDisplay();
        if(display) { display->clearScreen(); display->setBrightness8(brightness); drawScore(); }

        BLEDevice::init("Badminton_Score_BLE"); 
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        BLEService *pS = pServer->createService(SERVICE_UUID);
        pTxCharacteristic = pS->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
        pTxCharacteristic->addDescriptor(new BLE2902());
        BLECharacteristic *pRx = pS->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
        pRx->setCallbacks(new MyCharacteristicCallbacks()); 
        pS->start(); pServer->getAdvertising()->start();
        delay(500); startScoreAnnouncement(0, 0);
    }
}

void loop() {
    unsigned long now = millis();
    // WiFi 模式：處理 HTTP 請求，語音不需要運作
    if (isWifiMode) {
        server.handleClient();

        // --- WiFi 模式顯示狀態機 ---
        if (isGameOver) {
            // 1. 勝負動畫循環
            if(gifL_open&&now>=nextFrameL){ int d=0; if(gifL.playFrame(false,&d,(void*)0)) nextFrameL=now+(d<20?100:d); else{gifL.close();gifL.open(activeGifL_path.c_str(),GIFOpenFile,GIFCloseFile,GIFReadFile,GIFSeekFile,GIFDraw);nextFrameL=now+50;} }
            if(gifR_open&&now>=nextFrameR){ int d=0; if(gifR.playFrame(false,&d,(void*)1)) nextFrameR=now+(d<20?100:d); else{gifR.close();gifR.open(activeGifR_path.c_str(),GIFOpenFile,GIFCloseFile,GIFReadFile,GIFSeekFile,GIFDraw);nextFrameR=now+50;} }
        } else if (playEventGif) {
            // 2. 事件動畫 (SMASH/OUT) 單次播放
            if(!gifL_open){ if(display) display->clearScreen(); gifL_open=gifL.open(eventGifName.c_str(),GIFOpenFile,GIFCloseFile,GIFReadFile,GIFSeekFile,GIFDrawSS); nextFrameL=now; }
            if(gifL_open&&now>=nextFrameL){ int d=0; if(gifL.playFrame(false,&d,NULL)) nextFrameL=now+(d<20?100:d); else{gifL.close();gifL_open=false;playEventGif=false;needsUpdateUI=true;} }
        } else if (gifL_open && now < testEndTime) {
            // 3. 設定頁測試播放 GIF
            if(now>=nextFrameL){ int d=0; if(gifL.playFrame(false,&d,NULL)) nextFrameL=now+(d<20?100:d); else{gifL.close();gifL.open(activeGifTest_path.c_str(),GIFOpenFile,GIFCloseFile,GIFReadFile,GIFSeekFile,GIFDrawSS);nextFrameL=now+50;} }
        } else if (gifL_open) {
            // 4. 測試播放時間到，恢復計分顯示
            closeGIFs(); wifiUiDrawn=false; needsUpdateUI=true;
        } else if (needsUpdateUI) {
            // 5. 有計分時顯示計分板
            if(scoreA>0||scoreB>0){ drawScore(); needsUpdateUI=false; }
            else if(!wifiUiDrawn) {
                // 6. 閒置時顯示 WiFi 連線資訊
                display->clearScreen();
                display->setTextSize(1);
                display->setTextColor(display->color565(0,255,255)); display->setCursor(1,2);  display->print("WiFi: Badminton");
                display->setCursor(1,12); display->print("_Setup");
                display->setTextColor(display->color565(255,255,0)); display->setCursor(1,22); display->print("192.168.4.1");
                wifiUiDrawn=true; needsUpdateUI=false;
            }
        }
    } else {
        processAudioState();
        if (bleDoReset) { scoreA=0; scoreB=0; isGameOver=false; inScreenSaver=false; playEventGif=false; closeGIFs(); if(display) display->clearScreen(); needsUpdateUI=true; bleDoReset=false; startScoreAnnouncement(0, 0); }
        if (bleCheckScore) {
            bool go = ((scoreA>=21||scoreB>=21)&&(abs(scoreA-scoreB)>=2||scoreA==30||scoreB==30));
            if (go && !isGameOver) {
                isGameOver=true; if(display) display->clearScreen(); playEventGif=false;
                activeGifL_path = safePath((scoreA > scoreB) ? gifWin_name : gifLose_name); activeGifR_path = safePath((scoreB > scoreA) ? gifWin_name : gifLose_name);
                gifL_open = gifL.open(activeGifL_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw);
                gifR_open = gifR.open(activeGifR_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw);
                nextFrameL = now; nextFrameR = now;
            } else if (!go && isGameOver) { isGameOver=false; closeGIFs(); if(display) display->clearScreen(); }
            needsUpdateUI=true; bleCheckScore=false;
        }
        
        if (now - lastActionTime > 180000 && !isGameOver && !playEventGif) {
            if (!inScreenSaver) { inScreenSaver=true; closeGIFs(); if(display) display->clearScreen(); activeGifTest_path=safePath(gifSS_name); if(activeGifTest_path!="/") gifL_open=gifL.open(activeGifTest_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDrawSS); nextFrameL=now; }
            if (gifL_open && now >= nextFrameL) { int d=0; if (gifL.playFrame(false, &d, NULL)) nextFrameL = now + (d<20?100:d); else { gifL.close(); gifL.open(activeGifTest_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDrawSS); nextFrameL = now+50; } }
        } else {
            if (inScreenSaver) { inScreenSaver=false; closeGIFs(); if(display) display->clearScreen(); needsUpdateUI=true; }
            if (isGameOver) {
                if(gifL_open && now>=nextFrameL){ int d=0; if(gifL.playFrame(false,&d,(void*)0)) nextFrameL=now+(d<20?100:d); else { gifL.close(); gifL.open(activeGifL_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw); nextFrameL=now+50; } }
                if(gifR_open && now>=nextFrameR){ int d=0; if(gifR.playFrame(false,&d,(void*)1)) nextFrameR=now+(d<20?100:d); else { gifR.close(); gifR.open(activeGifR_path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw); nextFrameR=now+50; } }
            } else if (playEventGif) {
                // ✅ 單次事件動畫播放 (殺球 / 界外)
                if (!gifL_open) {
                    if(display) display->clearScreen();
                    gifL_open = gifL.open(eventGifName.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDrawSS);
                    nextFrameL = now;
                }
                if (gifL_open && now >= nextFrameL) {
                    int d=0; 
                    if(gifL.playFrame(false,&d,NULL)) {
                        nextFrameL=now+(d<20?100:d);
                    } else {
                        // 播放完畢
                        gifL.close(); gifL_open=false; playEventGif=false; needsUpdateUI=true;
                    }
                }
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