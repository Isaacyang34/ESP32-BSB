#include <RCSwitch.h>

RCSwitch mySwitch = RCSwitch();

// 請將 SYN480R 的 DATA 腳位接至此 GPIO (可依據您的實際接線修改)
#define RF_RECEIVER_PIN 4 

void setup() {
  Serial.begin(115200);
  
  // 初始化 433MHz 接收腳位 (必須是支援外部中斷的腳位)
  mySwitch.enableReceive(digitalPinToInterrupt(RF_RECEIVER_PIN));  
  
  Serial.println("=====================================");
  Serial.println("433MHz (SYN480R) 遙控器解碼測試程式");
  Serial.print("目前監聽的 ESP32 GPIO 腳位: ");
  Serial.println(RF_RECEIVER_PIN);
  Serial.println("請按下 EV1527 四鍵遙控器上的按鈕...");
  Serial.println("=====================================");
}

void loop() {
  if (mySwitch.available()) {
    long int value = mySwitch.getReceivedValue();
    
    if (value == 0) {
      Serial.println("收到未知的編碼格式");
    } else {
      Serial.print("收到按鍵代碼 (Decimal): ");
      Serial.print(value);
      Serial.print(" | 二進位 (Binary): ");
      
      // 印出二進位以便觀察 EV1527 的 20 bit 內碼 + 4 bit 按鍵碼結構
      int bitLength = mySwitch.getReceivedBitlength();
      for (int i = bitLength - 1; i >= 0; i--) {
        Serial.print(bitRead(value, i));
      }
      
      Serial.print(" | 長度 (Bit): ");
      Serial.print(bitLength);
      Serial.print(" | 協議: ");
      Serial.println(mySwitch.getReceivedProtocol());
    }
    
    // 準備接收下一個訊號
    mySwitch.resetAvailable();
  }
}
