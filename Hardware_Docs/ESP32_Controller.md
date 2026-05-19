# ESP32-S3 主控制器 (Main Controller)

> ⚠️ **本文件以自製 PCB 為準**：`ESP32-S3-HUB75-74HCT245`（含 SY8089 降壓器版本，2026-03-31）

---

## 1. 核心模組規格

### ESP32-S3-WROOM-1U (N16R16V)
| 規格項目 | 數值 |
| :--- | :--- |
| **型號（BOM/P&P 確認）** | `ESP32-S3-WROOM-1U-N16R16V` |
| **天線** | 外部 U.FL 天線（1U = 外接天線版） |
| **CPU** | Xtensa LX7 雙核心 @ **240MHz** |
| **Flash** | **16MB** (QIO 80MHz) |
| **PSRAM** | **16MB** OPI PSRAM（R16V = 16MB Octal） |
| **無線** | Wi-Fi 802.11 b/g/n + BLE 5.0 |
| **AI 加速** | PIE 向量指令集（支援 ESP-SR / TFLite Micro） |
| **內部 SRAM** | 512KB |

> **補充說明**：Arduino IDE 截圖顯示 `OPI PSRAM`，BOM 中為 `ESP32-S3-WROOM-1(N16R8)`，
> 但 Pick & Place 檔案顯示實際焊接的是 `N16R16V`（16MB PSRAM）。
> **以 Pick & Place 為準，PSRAM = 16MB**。

### Arduino IDE 燒錄設定（已確認）
| 設定項目 | 數值 |
| :--- | :--- |
| 板子類型 | ESP32S3 Dev Module |
| 連接埠 | COM7 |
| CPU Frequency | 240MHz (WiFi) |
| Flash Mode | QIO 80MHz |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Partition Scheme | 16M Flash (3MB APP / 9.9MB FATs) |
| Arduino Runs On | Core 1 |
| Events Run On | Core 1 |
| USB CDC On Boot | Enabled |
| Upload Mode | UART0 / Hardware CDC |
| Upload Speed | 512000 |
| USB Mode | Hardware CDC and JTAG |

---

## 2. 自製 PCB 架構（ESP32-S3-HUB75-74HCT245）

### 主要元件清單（BOM）

| 元件位號 | 型號 | 功能說明 |
| :--- | :--- | :--- |
| **U1** | ESP32-S3-WROOM-1U-N16R16V | 主控制器 |
| **U2, U3** | MC74HCT245ADWR2 | 74HCT245 雙向總線緩衝器 × 2（3.3V → 5V 準位轉換，驅動 HUB75） |
| **U4, U6, U7, U8** | 146268-3（TE 6-pin 擴充座）| 各功能擴充接頭 × 4 |
| **U11** | SY8089AABC | DC-DC 降壓 IC（SOT-23-5，提供穩定電壓） |
| **U5** | AMS1117-3.3 | LDO 3.3V 穩壓（ESP32 供電） |
| TYPE-C | TYPE-C-31-M-37A | USB Type-C 供電 / 燒錄接口 |
| BOOT / RST | DS-03XG-160H5 | 開機 / 重置按鈕 |
| C8 | 1000uF | 主電源濾波電容 |
| L1 | 2.2uH | SY8089 電感 |
| D1 | SS34-MS | 肖特基二極體（防倒灌） |
| R3, R4 | 5.1kΩ | USB CC 下拉電阻（Type-C 識別） |
| MODE-0 | DS1023-02-2X8SF11 | 16-pin DIP 模式設定排針（背面） |
| MODE-1 | SH-JN2.54-2-8P-Z | IDC 2×8 模式設定座 |

### 74HCT245 的作用
HUB75 LED 面板訊號為 **5V 邏輯**，而 ESP32-S3 GPIO 輸出為 **3.3V**。
`MC74HCT245` 是 5V 相容的 8-bit 三態總線緩衝器，負責將 3.3V 訊號升壓至 5V，確保面板驅動穩定。

---

## 3. 擴充接頭腳位圖（U4 / U6 / U7 / U8）

> 依照 `U4.jpg` 內的腳位圖：

### U4（左上，6-pin）
| 腳位 | 訊號 | 說明 |
| :--- | :--- | :--- |
| 1 | IO1 | GPIO 1 |
| 2 | TXD | UART TX |
| 3 | IO2 | GPIO 2 |
| 4 | RXD | UART RX |
| 5 | IO41 | GPIO 41 |
| 6 | IO42 | GPIO 42 |

### U6（右下，6-pin）
| 腳位 | 訊號 | 說明 |
| :--- | :--- | :--- |
| 1 | IO3 | GPIO 3 |
| 2 | IO13 | GPIO 13 |
| 3 | GND | 地 |
| 4 | IO14 | GPIO 14 |
| 5 | GND | 地 |
| 6 | IO21 | GPIO 21 |

### U7（左下，6-pin）
| 腳位 | 訊號 | 說明 |
| :--- | :--- | :--- |
| 1 | IO40 | GPIO 40 → **語音模組 RX（CX1000AC TX）** |
| 2 | IO39 | GPIO 39 → **語音模組 TX（CX1000AC RX）** |
| 3 | IO38 | GPIO 38 |
| 4 | 3V3 | 3.3V 電源輸出 |
| 5 | 5V | 5V 電源輸出 |
| 6 | GND | 地 |

### U8（右上，6-pin）
| 腳位 | 訊號 | 說明 |
| :--- | :--- | :--- |
| 1 | IO45 | GPIO 45 |
| 2 | IO48 | GPIO 48 |
| 3 | IO46 | GPIO 46 |
| 4 | 3V3 | 3.3V 電源輸出 |
| 5 | IO47 | GPIO 47 |
| 6 | 5V | 5V 電源輸出 |

---

## 4. 已使用的 GPIO（韌體確認）

| GPIO | 功能 | 連接對象 |
| :--- | :--- | :--- |
| IO4 | HUB75 R1 | LED 面板 |
| IO5 | HUB75 G1 | LED 面板 |
| IO6 | HUB75 B1 | LED 面板 |
| IO7 | HUB75 R2 | LED 面板 |
| IO8 | HUB75 C | LED 面板 |
| IO9 | HUB75 D | LED 面板 |
| IO10 | HUB75 CLK | LED 面板 |
| IO11 | HUB75 LAT | LED 面板 |
| IO12 | HUB75 OE | LED 面板 |
| IO14 | HUB75 E | LED 面板 |
| IO15 | HUB75 G2 | LED 面板 |
| IO16 | HUB75 B2 | LED 面板 |
| IO17 | HUB75 A | LED 面板 |
| IO18 | HUB75 B | LED 面板 |
| **IO39** | AudioSerial TX | CX1000AC 語音模組 RX（U7-2） |
| **IO40** | AudioSerial RX | CX1000AC 語音模組 TX（U7-1） |
| IO38 | BUSY 訊號（備用） | CX1000AC BUSY（U7-3） |

---

## 5. 可用擴充 GPIO（未使用）

| 接頭 | 可用 GPIO | 建議用途 |
| :--- | :--- | :--- |
| U4 | IO1, IO2, IO41, IO42 + TXD/RXD | 備用 UART / 一般 IO |
| U6 | IO3, IO13, IO14, IO21 | I2C / SPI / 一般 IO |
| U7 | IO38（已佔部分）| BUSY 偵測 / 其他 |
| U8 | IO45, IO46, IO47, IO48 | **I2S 麥克風（INMP441）最佳候選腳位** |

> **ESP-SR / INMP441 麥克風接線建議**（待實測）：
> - WS → IO45
> - SCK → IO46
> - SD → IO47
> - 使用 U8 接頭，3V3 / GND 同接頭已有

---

## 6. 關鍵能力評估

| 功能 | 評估 |
| :--- | :--- |
| ESP-SR 語音辨識 | ✅ 可行：16MB PSRAM 足夠，PIE 加速支援 |
| TFLite Micro | ✅ 可行：同上 |
| INMP441 麥克風 + ESP-SR | ⚠️ I2S 衝突待確認（HUB75 使用 I2S DMA） |
| BLE + WiFi 同時 | ❌ ESP32-S3 不支援 BT Classic，BLE + WiFi 同時可但有干擾 |
| BLE 喇叭 | ❌ 不支援 BT Classic A2DP |
