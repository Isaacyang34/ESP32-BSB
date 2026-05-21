# ESP-SR 語音辨識整合說明

> 適用硬體：`ESP32-S3-HUB75-74HCT245` 自製 PCB
> ESP32 模組：`ESP32-S3-WROOM-1U-N16R16V`（16MB Flash + 16MB OPI PSRAM）

---

## 1. 什麼是 ESP-SR？

**ESP-SR** 是 Espressif（樂鑫）官方提供的語音辨識框架，專為 ESP32-S3 設計，完全在裝置上本地執行，不需要網路或雲端服務。

它由三個主要元件組成：

```
麥克風輸入
    ↓
[AFE] 音頻前端處理（降噪 / 回音消除 / 語音活動偵測）
    ↓
[WakeNet] 喚醒詞偵測（持續監聽，如「小智同學」）
    ↓
[MultiNet] 命令詞辨識（偵測到喚醒詞後才啟動）
    ↓
觸發對應動作（加分 / 特效 / 重置）
```

---

## 2. 硬體需求與本板規格對照

| 需求項目 | ESP-SR 最低要求 | 本板實際規格 | 是否符合 |
| :--- | :--- | :--- | :--- |
| CPU | ESP32-S3 @ 240MHz | ✅ 240MHz 雙核 LX7 | ✅ |
| PSRAM | ≥ 4MB | ✅ **16MB OPI PSRAM** | ✅ 遠超需求 |
| Flash | ≥ 4MB | ✅ 16MB | ✅ |
| 麥克風 | I2S 數位麥克風 | ⚠️ 需額外購買 INMP441 | 需採購 |
| I2S 通道 | 1 個 I2S 外設 | ⚠️ HUB75 已佔用一個 I2S | 需確認 |

### ⚠️ I2S 衝突問題說明
本板使用 `ESP32-HUB75-MatrixPanel-I2S-DMA` 函式庫驅動 LED 面板，
該函式庫**佔用 I2S 外設**作為 DMA 輸出。

ESP32-S3 共有 **2 個 I2S 外設**（I2S0 / I2S1），
理論上 HUB75 用 I2S0，INMP441 麥克風用 I2S1，可以並存，
但實際上**需要測試確認**函式庫是否寫死使用特定 I2S。

---

## 3. 所需硬體採購

### INMP441 數位 MEMS 麥克風模組

| 規格 | 數值 |
| :--- | :--- |
| 介面 | I2S（數位，無需 ADC） |
| 電壓 | 3.3V |
| 尺寸 | 約 2 × 2 cm 模組 |
| 參考價格 | 約 40~80 元（台幣）|

#### 接線建議（接至 U8 擴充座）

```
INMP441        →   U8 腳位      →   GPIO
──────────────────────────────────────────
VDD            →   腳 4（3V3）  →   3.3V
GND            →   腳 6（GND 附近，需確認）→ GND
L/R（左右聲道）→   GND          →   單聲道
SCK（時脈）    →   腳 3（IO46） →   GPIO46
WS（字選）     →   腳 1（IO45） →   GPIO45
SD（資料）     →   腳 5（IO47） →   GPIO47
```

> **選擇 U8 的原因**：U8 接頭提供 IO45/IO46/IO47/IO48 + 3V3 + 5V，
> 這幾個 GPIO 在韌體中目前**未被使用**，適合作為 I2S 麥克風腳位。

---

## 4. 軟體框架安裝

### 方法 A：使用 ESP-IDF（官方，完整功能）

```bash
# 安裝 ESP-IDF v5.x
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3

# 加入 ESP-SR 元件
idf.py add-dependency "espressif/esp-sr"
```

### 方法 B：Arduino IDE（簡化整合，本專案適用）

Arduino 環境下使用 `ESP32-audioI2S` + ESP-SR 預編譯函式庫：

```
函式庫管理員搜尋並安裝：
- ESP32-audioI2S（麥克風讀取）
- espressif/esp-sr（需手動加入 components 資料夾）
```

> ⚠️ **注意**：ESP-SR 在 Arduino 框架的支援度不如 ESP-IDF 完整，
> 建議最終整合時改用 ESP-IDF 開發。

---

## 5. 效能消耗（本板可用資源評估）

### 各元件 CPU 佔用（ESP32-S3 @ 240MHz）

| 元件 | CPU 核心 | 持續佔用率 | 備註 |
| :--- | :--- | :--- | :--- |
| AFE（降噪處理） | Core 0 | ~30% | 全程常駐 |
| WakeNet（喚醒詞） | Core 1 | ~10% | 全程常駐 |
| MultiNet（命令詞） | Core 1 | ~40% | 僅喚醒後短暫啟動 |
| **ESP-SR 待機合計** | — | **~40% 單核** | — |

### 本專案現有佔用

| 任務 | 核心 | 佔用率 |
| :--- | :--- | :--- |
| HUB75 DMA 輸出 | 背景 DMA | 極低 CPU |
| GIF 解碼播放 | Core 1 | 播放時 ~30~50% |
| BLE 協定棧 | Core 0 | ~15~20% |
| WebServer（WiFi 模式） | Core 0 | 請求時 ~10% |

### 雙核分配建議

```
Core 0：BLE 協定棧 + ESP-SR AFE + WakeNet
         ├── BLE：~20%
         ├── AFE：~30%
         └── WakeNet：（WakeNet 通常在 Core 1，可調整）
         合計：~50%，有餘裕

Core 1：Arduino loop() + HUB75 + GIF 解碼
         ├── HUB75 DMA：~5%（背景）
         ├── GIF 解碼：~30~50%（播放時）
         └── MultiNet：~40%（喚醒瞬間）
         ⚠️ GIF 播放 + MultiNet 同時啟動時可能接近滿載
```

**結論**：平常使用沒問題，GIF 播放期間同時辨識命令有輕微頓幀風險，
可透過「辨識完再播動畫」的邏輯設計來迴避。

---

## 6. 建議的語音指令設計

### 喚醒詞 → 命令詞 兩段式流程

```
裁判說：「小智同學」（喚醒詞，WakeNet 偵測）
  → 裝置 LED 面板短閃提示「聽到了」
裁判說：「A 隊加分」（命令詞，MultiNet 辨識）
  → ESP32 執行 scoreA++ 並更新顯示
```

### 建議命令詞清單（MultiNet 中文支援）

| 說出的詞 | 對應指令 |
| :--- | :--- |
| A 隊加分 / 左邊加分 | `scoreA++` |
| B 隊加分 / 右邊加分 | `scoreB++` |
| A 隊減分 / 左邊減分 | `scoreA--` |
| B 隊減分 / 右邊減分 | `scoreB--` |
| 重設分數 / 比賽開始 | Reset |
| 殺球 | SMASH 特效 |
| 界外 | OUT 特效 |

> MultiNet 支援最多 **200 個中文命令**，每個命令可設定多個同義詞。

---

## 7. 與現有系統的整合方式

### 整合後系統架構

```
現有系統：
手機 BLE → ESP32 → 計分 / GIF / 語音報分

加入 ESP-SR 後：
INMP441 麥克風 → ESP-SR（本機） → 計分 / GIF / 語音報分
手機 BLE → ESP32 → 計分 / GIF / 語音報分（保留作備用）
```

### 韌體修改重點

1. **新增 I2S 麥克風讀取任務**（獨立 FreeRTOS task，分配到 Core 0）
2. **WakeNet 常駐偵測**
3. **MultiNet 命令詞對應原有的 BLE 指令處理函式**
4. **喚醒提示**：LED 面板顯示短暫閃爍或音效提示

### 不影響現有功能的設計

- BLE 計分功能**完整保留**，語音辨識為**額外輸入源**
- 兩者共享同一套計分邏輯（`scoreA++`, `scoreB++` 等）
- 語音報分（CX1000AC）繼續運作不受影響

---

## 8. 開發路線建議

```
階段 1（硬體驗證）：
  → 購買 INMP441 模組
  → 接至 U8 擴充座（IO45/IO46/IO47）
  → 用 Arduino I2S 範例確認能讀取音訊

階段 2（I2S 衝突確認）：
  → 同時開啟 HUB75 DMA + I2S 麥克風
  → 確認 ESP32-HUB75 函式庫使用的是 I2S0 還是 I2S1
  → 若衝突：麥克風改用 I2S0（或反之）

階段 3（ESP-SR 整合）：
  → 改用 ESP-IDF 環境開發（Arduino 作為元件使用）
  → 整合 AFE + WakeNet + MultiNet
  → 測試在球場噪音環境下的辨識準確率

階段 4（正式整合）：
  → 合併至主韌體 esp32.ino（或拆成多檔）
  → 完整測試 BLE + ESP-SR + HUB75 + 語音報分同時運作
```

---

## 9. 參考資源

| 資源 | 連結 |
| :--- | :--- |
| ESP-SR GitHub | https://github.com/espressif/esp-sr |
| ESP-SR 官方文件 | https://docs.espressif.com/projects/esp-sr/ |
| MultiNet 中文命令列表 | https://github.com/espressif/esp-sr/blob/master/docs/zh_CN/speech_command_recognition/README.md |
| INMP441 資料手冊 | InvenSense INMP441 Datasheet |
| ESP32-S3 技術參考手冊 | https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf |
