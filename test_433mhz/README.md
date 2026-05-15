# 433MHz (SYN480R + EV1527) 遙控器測試說明

針對羽球計分板專案，若要加入 433MHz 遙控模組，請先使用此測試程式 (`test_433mhz.ino`) 取得您實體遙控器上每一顆按鈕的專屬代碼。

## 1. 安裝必要的函式庫
在開啟 `test_433mhz.ino` 前，請確認您的 Arduino IDE 已經安裝了 433MHz 專用的解碼函式庫：
- 開啟 Arduino IDE 的 **Library Manager (函式庫管理員)**。
- 搜尋 **`rc-switch`** (作者為 sui77)。
- 點擊安裝。

## 2. 硬體接線 (SYN480R ➜ ESP32)
- **VCC**: 接 ESP32 的 `3.3V` 或 `5V` (通常接 5V 接收距離較好)。
- **GND**: 接 ESP32 的 `GND`。
- **DATA / OUT**: 預設請接在 ESP32 的 **GPIO 4** 
  *(如果您接在其他腳位，請打開 `.ino` 檔並修改 `#define RF_RECEIVER_PIN 4`)*。

## 3. 燒錄與擷取代碼
- 將 `test_433mhz.ino` 燒錄到您的 ESP32 上。
- 打開 Arduino IDE 的 **Serial Monitor (序列埠監控視窗)**，並將鮑率設定為 `115200`。
- 此時按下您 EV1527 遙控器上的 A、B、C、D 四個按鈕。
- 監控視窗就會印出每個按鍵對應的 **Decimal (十進位) 代碼**。

### 下一步
請將 Serial Monitor 上出現的這四組 Decimal 數字記錄下來，之後我們就可以將這四組號碼寫入主程式 (`esp32.ino`) 中，分別對應「A隊加分」、「B隊加分」、「減分」或是「換發球」等功能了！
