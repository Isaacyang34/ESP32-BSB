# ESP32-S3 主控制器 (Main Controller)

## 1. 硬體規格 (Specifications)
*   **核心晶片**：ESP32-S3 (具備雙核心處理器)
*   **無線通訊**：內建 Wi-Fi 與 **BLE (Bluetooth Low Energy) 5.0**
*   **硬體資源**：支援 I2S DMA (直接記憶體存取)，對於刷新高解析度 LED 面板極為重要，可達到不閃爍且不佔用 CPU 的顯示效果。

## 2. 專案功能 (Functions)
在羽球計分板專案中，ESP32 扮演「大腦」的角色，統籌所有模組：
1.  **藍牙通訊 (BLE Server)**：建立 BLE 廣播，接收來自手機前端網頁 (`Badminton_Score_BLE.html`) 發送的字元指令 (`A`, `B`, `R` 等)。
2.  **畫面渲染**：計算比分狀態後，透過 I2S DMA 將畫面推送到 HUB75 LED 面板。
3.  **語音驅動**：將要播放的分數加入非阻塞式佇列 (Queue)，透過 UART 發送指令給 CX1000AC。
4.  **遙控器讀取**：透過硬體中斷 (Interrupt) 即時監聽 SYN480R 傳來的 433MHz 遙控器代碼。

## 3. 接腳定義 (Pinout & Connections)
*下表僅列出與擴充模組相關的客製化腳位，HUB75 面板腳位依據函式庫預設或您的硬體轉接板而定。*

| ESP32 腳位 | 連接對象 | 功能說明 |
| :--- | :--- | :--- |
| **GPIO 39** | CX1000AC 的 `RX` | UART TX (傳送語音播放指令) |
| **GPIO 40** | CX1000AC 的 `TX` | UART RX (接收語音模組回傳狀態) |
| **GPIO 4** | SYN480R 的 `DATA` | 接收 433MHz 遙控器解碼訊號 (設定為外部中斷) |
| **5V / VIN** | 所有模組的 `VCC` | 提供 5V 工作電壓 |
| **GND** | 所有模組的 `GND` | 系統共地 |
