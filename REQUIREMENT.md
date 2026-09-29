# 電壓及車內空氣狀態監督站 系統需求規格書 (REQUIREMENT.md)

**文件版本**：1.4.0  
**最後更新時間**：2026-09-29  
**開發環境**：PlatformIO + Arduino Framework (ESP32)  
**UI 設計工具**：SquareLine Studio v1.5.0 + LVGL 8.3.11  

---

## 1. 專案概述 (Project Overview)

`電壓及車內空氣狀態監督站`（原 BTPower）是一套基於 **ESP32** 晶片開發的「多功能電池電力與車內環境品質監控終端 / 顯示中繼站」。

本系統主要負責透過 **ESP-NOW** 低延遲無線廣播協定，接收來自遠端電池監測節點（如車用電池、儲能電源）回傳的即時電壓、溫度、濕度遙測數據；同時本機整合 **GY-86 (BMP180)** 氣壓海拔感測器與 **JW01** 一體化空氣品質感測器（CO2、TVOC、甲醛 CH2O），並透過 2.8 吋彩屏 TFT 配合 LVGL 圖形化雙頁面純繁體中文儀表板進行即時觸控監控、異常警示與輪播切換。

---

## 2. 系統架構圖 (System Architecture)

```mermaid
graph TD
    subgraph 遠端節點 Remote Node
        Sensors[電壓 / 溫濕度檢測] --> RemoteESP[ESP32/ESP8266 節點]
    end

    RemoteESP -- "ESP-NOW 無線傳輸 (struct_message)" --> ReceiverESP[BTPower 主控站 (ESP32)]

    subgraph 本地周邊 Local Peripherals
        BMP180[GY-86 / BMP180 氣壓海拔模組] -- "I2C (SDA:22, SCL:27)" --> ReceiverESP
        JW01[JW01 空氣品質感測器] -- "UART (RX:35 9600bps)" --> ReceiverESP
        TFT[2.8吋 TFT 彩屏 (240x320)] <-- "SPI (TFT_eSPI)" -- ReceiverESP
        Touch[XPT2046 觸控晶片] --> "SPI (TFT_Touch)" --> ReceiverESP
        LED[RGB 狀態指示燈 (GPIO 4, 16, 17)] <-- "GPIO 輸出" -- ReceiverESP
    end

    subgraph 核心軟體架構 Core Firmware
        ReceiverESP --> Timer0[硬體計時器 0 (1秒 ISR 時鐘累加)]
        ReceiverESP --> Timer1[硬體計時器 1 (30秒輪播中斷)]
        ReceiverESP --> LVGL[LVGL 8.3 GUI 渲染引擎]
        LVGL --> Screen1[Screen 1: 電力與大氣儀表板]
        LVGL --> Screen2[Screen 2: 空氣品質監測儀表板]
    end
```

---

## 3. 硬體規格與接線定義 (Hardware Specifications)

### 3.1 主控與基礎周邊
- **主晶片**：ESP32-WROOM-32 / ESP32 Dev Module
- **工作電壓**：5V (經板載 LDO 轉 3.3V)
- **顯示模組**：2.8 吋 SPI TFT LCD (解析度 240 x 320，直屏模式 `Rotation(0)`)
- **觸控晶片**：XPT2046 電阻式觸控面板
- **螢幕背光**：GPIO 27 控制 (輸出高電平點亮)

### 3.2 腳位對應清單 (Pinout Mapping)

| 模組 / 元件 | 功能定義 | ESP32 腳位 | 備註說明 |
| :--- | :--- | :--- | :--- |
| **XPT2046 觸控** | DOUT (T_DO) | GPIO 39 | 觸控資料輸出 |
| **XPT2046 觸控** | DIN (T_DIN) | GPIO 32 | 觸控資料輸入 |
| **XPT2046 觸控** | DCS (T_CS) | GPIO 33 | 觸控晶片片選 (CS) |
| **XPT2046 觸控** | DCLK (T_CLK) | GPIO 25 | 觸控 SPI 時脈 |
| **TFT 螢幕** | Backlight (背光) | GPIO 27 | 數位輸出 (HIGH 開啟) |
| **RGB 指示燈** | 紅色 LED (LED_RED) | GPIO 4 | 電壓 < 12.5V 時觸發警示 |
| **RGB 指示燈** | 綠色 LED (LED_GREEN) | GPIO 16 | 系統正常運行呼吸跳動 |
| **RGB 指示燈** | 藍色 LED (LED_BLUE) | GPIO 17 | 系統狀態交替指示 |
| **GY-86 (BMP180)** | I2C SDA | GPIO 22 | 大氣壓力與海拔測量 |
| **GY-86 (BMP180)** | I2C SCL | GPIO 27 | 與背光共用腳位或需硬體跳線注意 |
| **JW01 空氣感測** | UART RX | GPIO 35 | 接 JW01 的 TX (9600 bps) |
| **JW01 空氣感測** | UART TX | GPIO 34 | 與光敏電阻共用，目前預設空接 |

---

## 4. 功能需求規格 (Functional Requirements)

### 4.1 ESP-NOW 遙測接收與節點管理
1. **傳輸協定**：採用 ESP-NOW 點對多點廣播接收，WiFi 模式初始化為 `WIFI_STA`。
2. **封包格式 (`struct_message`)**：
   - `float nVoltage`：遠端回傳電壓值（單位：V）。
   - `float nTemperature`：遠端回傳環境溫度值（單位：°C）。
   - `float nHumidity`：遠端回傳環境相對濕度（單位：%）。
   - `int CheckSUM`：檢查碼，計算公式需滿足 `CheckSUM == (nVoltage + nHumidity + nTemperature)`。
3. **資料驗證與快取**：
   - 封包必須經過 CheckSUM 驗證，校驗無誤後方可寫入內部資料庫陣列 `dataArray`（容量上限 256 筆）。
   - 自動將資料標註當前系統時間戳記 (`now()`)。
4. **多設備清單維護 (`connections`)**：
   - 支援最多 10 台連線設備 (`MAX_PEERS = 10`)。
   - 接收到封包時自動比對 MAC 位址，若為新設備則自動加入清單並更新累計連線數量 (`connectedDevices`)。
   - 累計更新總接收封包數 (`RecordCount`)。

### 4.2 大氣與高度監測 (BMP180)
1. **校正海平面壓力**：預設基準壓力設定為 `1008.0 hPa`（高雄地區校正標準）。
2. **輸出參數**：
   - 即時大氣壓力（單位：hPa，小數點後兩位）。
   - 換算海拔高度（單位：m 公尺，小數點後兩位）。
3. **例外防護**：感測器初始化失敗或讀取異常時輸出 Sensor Error Log，不影響主循環運作。

### 4.3 室內空氣品質監測 (JW01)
1. **通訊解析**：透過 `EspSoftwareSerial` 以 9600 鮑率監聽 9 位元組（bytes）之固定封包。
2. **封包校驗**：第 9 Byte 為前 8 Bytes 之累加和 (`packet[8] == sum(packet[0..7])`)。
3. **監測項目與警報閥值**：
   - **二氧化碳 (CO2)**：計算公式 `packet[6] * 256 + packet[7]` (ppm)。
     - 正常門檻：<= 3500 ppm（進度條顯示為綠色 `#00FF00`）。
     - 超標門檻：> 3500 ppm（進度條顯示為紅色 `#FF0000`）。
   - **總揮發性有機物 (TVOC)**：計算公式 `packet[2] * 256 + packet[3] * 0.001` (gm/m³)。
     - 正常門檻：<= 0.6 gm/m³（綠色進度條）。
     - 超標門檻：> 0.6 gm/m³（紅色進度條）。
   - **甲醛 (CH2O)**：計算公式 `packet[4] * 256 + packet[5] * 0.001` (gm/m³)。
     - 正常門檻：<= 0.15 gm/m³（綠色進度條）。
     - 超標門檻：> 0.15 gm/m³（紅色進度條）。

### 4.4 雙畫面 UI 介面與自動輪播
1. **UI 框架**：採用 SquareLine Studio 匯出之 LVGL 8.3 代碼。
2. **Screen 1：主電力與環境畫面**
   - **電壓區域**：即時電壓 (`ui_Label1`)、歷史最高電壓 (`ui_HighV`)、歷史最低電壓 (`ui_LowV`)。
   - **溫度區域**：即時溫度 (`ui_Label3`)、歷史最高溫度 (`ui_HighT`)、歷史最低溫度 (`ui_LowT`)。
   - **氣壓高度**：大氣壓力值 (`ui_Pvalue`)、海拔高度值 (`ui_Avalue`)。
   - **統計狀態**：本機 MAC (`ui_Label7`)、已連線設備數 (`ui_CQTY`)、累計封包數 (`ui_RecordCount`)、系統運行時鐘 (`ui_Label9`)、版本號 (`ui_Wifi`)。
3. **Screen 2：氣體品質畫面**
   - 橫條圖與數據：CO2、TVOC、CH2O 三項數值文字與動態條狀指示圖 (`ui_CO2Bar`, `ui_TVOCBar`, `ui_CH20Bar`)。
4. **定時自動輪播**：
   - 透過硬體計時器 1（Timer 1）設定 30 秒中斷。
   - 當監測到有效電壓時，每隔 30 秒自動在 Screen 1 與 Screen 2 之間淡入切換 (`LV_SCR_LOAD_ANIM_FADE_ON`, 500ms)。

### 4.5 警報與指示燈控制
1. **低電壓警報**：當最新電壓大於 0V 且小於 12.5V 時，觸發低電壓警示，紅色 LED 點亮。
2. **正常心跳指示**：電壓正常（>= 12.5V）或未接通時，綠藍 LED 依主循環週期交替跳動。

### 4.6 系統計時器中斷
- **Timer 0 (1 秒中斷)**：用於計時系統開機時間（時:分:秒，`Hr:Min:Sec`），透過 `portENTER_CRITICAL` 鎖定記憶體確保線程安全。
- **Timer 1 (30 秒中斷)**：用於畫面輪播切換旗標設定。

---

## 5. 數據結構與通訊協定定義

### 5.1 ESP-NOW 遙測結構 (`struct_message`)
```c
typedef struct struct_message {
    float nVoltage;      // 遙測電壓 (V)
    float nTemperature;  // 遙測溫度 (°C)
    float nHumidity;     // 遙測濕度 (%)
    int   CheckSUM;      // 校驗碼 (nVoltage + nHumidity + nTemperature)
} struct_message;
```

### 5.2 JW01 串口通訊協定 (9-Byte Packet)
```
Byte 0 ~ 1 : 保留/標頭
Byte 2 ~ 3 : TVOC 濃度 (Byte 2 * 256 + Byte 3 * 0.001)
Byte 4 ~ 5 : 甲醛 CH2O 濃度 (Byte 4 * 256 + Byte 5 * 0.001)
Byte 6 ~ 7 : 二氧化碳 CO2 濃度 (Byte 6 * 256 + Byte 7)
Byte 8     : 校驗碼 = (Byte 0 + Byte 1 + ... + Byte 7) & 0xFF
```

---

## 6. 編譯與分區規格 (Build & Partition Specs)

### 6.1 PlatformIO 設定 (`platformio.ini`)
- **Board**：`esp32dev`
- **Framework**：`arduino`
- **上傳速率**：`921600`
- **序列埠監視速率**：`115200`
- **依賴套件 (Libraries)**：
  - `bodmer/TFT_eSPI @ ^2.5.43`
  - `lvgl/lvgl @ 8.3.11`
  - `paulstoffregen/Time @ ^1.6.1`
  - `adafruit/Adafruit BMP085 Unified @ ^1.1.3`
  - `bodmer/TFT_Touch @ ^0.3`
  - `plerup/EspSoftwareSerial @ ^8.2.0`

### 6.2 Flash 分區配置 (`no_ota.csv`)
因 LVGL 及字型佔用較大 Flash 空間，關閉 OTA 雙分區架構，專案配置專用 `no_ota.csv`：
- `app0`：最大支援 **2MB** 應用程式空間 (0x200000)
- `spiffs`：約 **1.9MB** 檔案儲存空間 (0x1E0000)
- `coredump`：**64KB** (0x10000)

---

## 7. 版本演進與變更紀錄 (Changelog)

- **Ver 1.4.0 (現行版本)**：
  - 專案名稱正式定名為「電壓及車內空氣狀態監督站」。
  - 介面全繁體中文化，生成並嵌入專屬 CJK 16px 與 20px 點陣字型 (`ui_font_chinese_16`, `ui_font_chinese_20`)。
  - 觸控效率與即時性大幅優化：全面重構主循環（`loop`）為非阻塞多工架構（200Hz 輪詢），移除 `delay(800)` 阻塞。
  - 移除觸控讀取中的 Serial 阻塞列印，點擊螢幕即時切換響應（< 10ms），切換過渡動畫縮短至 200ms。
  - 優化 JW01 串口接收封包校驗機制，滿 9 位元組才進行讀取與 Checksum 比對。
- **Ver 1.3.1**：
  - 修正 CO2、CH2O、TVOC 數值邊界與錯誤輸出限制。
- **Ver 1.3**：
  - 新增 JW01 空氣品質感測器支援，採用 GPIO 35 (RX) 軟體串口讀取。
- **Ver 1.2**：
  - 整合 `TFT_Touch` 驅動函式庫，支援 XPT2046 電阻觸控校準與事件響應。
- **Ver 1.1**：
  - 新增 GY-86 / BMP180 氣壓與海平面高度運算，調整 Screen 1 顯示排版。
- **Ver 1.0**：
  - 初版發布：基礎 ESP-NOW 電池遙測電壓接收與 2.8 吋 LVGL 介面顯示。
