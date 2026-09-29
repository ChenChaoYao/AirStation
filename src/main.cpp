/*
    Ver 1.4.0
        1. 程式全面繁體中文化，名稱定為「電壓及車內空氣狀態監督站」
        2. 導入專屬中文字型 ui_font_chinese_16 與 ui_font_chinese_20
        3. 移除 loop() 中阻塞式 delay(800)，改以 millis() 非阻塞多工排程
        4. 移除觸控中斷中的 Serial 列印阻礙，LVGL 每 5ms 即時輪詢處理觸控事件
        5. 縮減畫面切換動畫至 200ms，大幅提升觸控流暢度與即時反饋感
        6. 優化 JW01 串口接收封包長度校驗
    Ver 1.3.1
        修正 CO2，CH20，TVOC 的輸出，限制錯誤範圍
    Ver 1.3
        加上 JW01 CO2,CH20,TVOC CHECK Sensor 利用 GPIO 35 做為 RX 收取資料
    Ver 1.2
        加上 TFT_Touch library
    Ver 1.1
        加上 BMP180 做氣壓及海拔計算並修正畫面顯示
*/
#include <Arduino.h>
#define Version "1.4.0"
#define APP_TITLE "電壓及車內空氣狀態監督站"

#include <SPI.h>
#include <TFT_eSPI.h>
// ESPNOW 加载需要的库
#include <WiFi.h>
#include <esp_now.h>
// #include <ArduinoOTA.h>
// #include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <TimeLib.h> // 引入時間庫

#include <TFT_Touch.h>

#define GY_86 true
#define JW01 true

#ifdef GY_86
#include <Adafruit_BMP085_U.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#define SDA 22
#define SCL 27

// 國際標準海平面氣壓 (ISA 標準：1013.25 hPa)
#define SEALEVELPRESSURE_HPA (1013.25)
#endif

#ifdef JW01
#include <SoftwareSerial.h>
// 配置 UART 引腳
#define RX_PIN 35 // GPIO3: SuperMini RX 引腳接 JW01 的 TX
#define TX_PIN                                                                 \
    34 // GPIO4: SuperMini TX 引腳接 JW01 的 RX
       // ，此腳與光敏電阻共用，目前空接無效。
#define JW01_BAUD_RATE 9600
EspSoftwareSerial::UART CO2Serial;
#endif

// 定義一個結構來存儲數據和時間
struct DataWithTime {
    uint8_t data[15]; // 假設LEN是incomingData的長度
    time_t timestamp;
};

// 紀錄存儲數據的索引
int dataIdx = 0;

#define Hostname "8266-CAR-B-DISP"
#define LED_RED 4
#define LED_BLUE 17
#define LED_GREEN 16

#define MAX_DATA_SIZE 256
#define MAX_PEERS 10         // 最大設備數量
boolean rec_success = false; // 資料接收成功

// 創建一個存儲數據的陣列
DataWithTime dataArray[MAX_DATA_SIZE]; // 假設MAX_DATA_SIZE是最大存儲數據量

// #define WIFI_SSID "nubia Z50"
// #define WIFI_PASSWORD "8888888888"

// 連線資訊結構
typedef struct {
    uint8_t macAddress[6];
    bool isConnected;
} ConnectionInfo;

// 初始化連線清單
ConnectionInfo connections[MAX_PEERS];

// 計數器初始化
byte connectedDevices = 0;
int RecordCount = 0;

// 接收数据的结构示例
// 在C中使用 typedef struct 定义一个结构体类型,名为struct_message
// 必须与发送方的结构相匹配一致
typedef struct struct_message {
    float nVoltage;
    float nTemperature;
    float nHumidity;
    int CheckSUM;
} struct_message;

#define USE_UI     // if you want to use the ui export from Squareline ,pleease
                   // define USE_UI.
#define Display_28 // according to the board you using ,if you using the ESP32
                   // Display 3.5inch board, please define 'Display_35'.if
                   // using 2.4inch board,please define 'Display_24'.

#ifdef USE_UI
#include "ui.h"
#include <lvgl.h>
#endif

#if defined Display_35 // ESP32 Display 3.5inch Board
/*screen resolution*/
static const uint16_t screenWidth = 480;
static const uint16_t screenHeight = 320;
uint16_t calData[5] = {353, 3568, 269, 3491, 7}; /*touch caldata*/

#elif defined Display_24 // ESP32 Display 2.4inch Board
static const uint16_t screenWidth = 320;
static const uint16_t screenHeight = 240;
uint16_t calData[5] = {557, 3263, 369, 3493, 3};

#elif defined Display_28 // ESP32 Display 2.8inch Board
static const uint16_t screenWidth = 240;
static const uint16_t screenHeight = 320;
// uint16_t calData[5] = { 189, 3416, 359, 3439, 1 };
#endif

// These are the pins used to interface between the 2046 touch controller and
// Arduino Pro
#define DOUT 39 /* Data out pin (T_DO) of touch screen */
#define DIN 32  /* Data in pin (T_DIN) of touch screen */
#define DCS 33  /* Chip select pin (T_CS) of touch screen */
#define DCLK 25 /* Clock pin (T_CLK) of touch screen */

/* Create an instance of the touch screen library */
TFT_Touch touch = TFT_Touch(DCS, DCLK, DIN, DOUT);

TFT_eSPI lcd = TFT_eSPI(); /* TFT entity */

#if defined USE_UI
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[screenWidth * screenHeight / 13];

//_______________________
/* display flash */
void my_disp_flush(
    lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.pushColors((uint16_t *)&color_p->full, w * h, true);
    lcd.endWrite();

    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    uint16_t touchX, touchY;

    bool touched = touch.Pressed();

    if (!touched) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        touchX = touch.X();
        touchY = touch.Y();

        data->state = LV_INDEV_STATE_PR;

        /*Set the coordinates*/
        data->point.x = touchX;
        data->point.y = touchY;
    }
}

#endif

int Hr, Min, Sec;
int Screen = 0;

volatile bool state = LOW;
hw_timer_t *Timer0_Cfg = NULL;
hw_timer_t *timer = NULL;

portMUX_TYPE mux0 = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR Timer0_ISR() {
    portENTER_CRITICAL(&mux0); // 要鎖住記憶體區塊,不然跑一跑就爆掉了
    Sec++;
    if (Sec == 60) {
        Sec = 0;
        Min++;
        if (Min == 60) {
            Min = 0;
            Hr++;
        }
    }
    portEXIT_CRITICAL(&mux0); // 要鎖住記憶體區塊,不然跑一跑就爆掉了
}

String oVoltage;
String oTemperature;

// 创建 结构为struct_message的myData变量
struct_message myData;

// 將 MAC 地址加入到連線清單中
void addConnection(const uint8_t *macAddress) {
    // 檢查是否已經存在於連線清單中
    for (int i = 0; i < MAX_PEERS; i++) {
        if (connections[i].isConnected &&
            memcmp(connections[i].macAddress, macAddress, 6) == 0) {
            Serial.println("Device already exists in connections list");
            return;
        }
    }
    // 如果不存在，則加入到連線清單中
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!connections[i].isConnected) {
            memcpy(connections[i].macAddress, macAddress, 6);
            connections[i].isConnected = true;
            connectedDevices++;
            Serial.println(connectedDevices);
            Serial.println("Device added to connections list");
            return;
        }
    }
}

// 当收到数据时将执行的回调函数
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    memcpy(&myData, incomingData, sizeof(myData));
    int check = myData.nVoltage + myData.nHumidity + myData.nTemperature;
    if (myData.CheckSUM == check) {
        rec_success = true;

        // 將 MAC 地址添加到連線清單中
        addConnection(mac);

        // 將接收到的数据添加到数据数组中
        dataArray[dataIdx].data[0] = myData.nVoltage;
        dataArray[dataIdx].data[1] = myData.nTemperature;
        dataArray[dataIdx].data[2] = myData.nHumidity;
        addConnection(mac);
        RecordCount++;
        // 將incomingData存儲到dataArray中
        if (dataIdx < MAX_DATA_SIZE) {
            memcpy(dataArray[dataIdx].data, incomingData, len);
            dataArray[dataIdx].timestamp = now(); // 獲取當前時間戳記
            dataIdx++;
        }
        // Serial.print("Bytes received: ");
        // Serial.println(len);
        // Serial.print("Voltage: ");
        // Serial.println(myData.nVoltage);
        // Serial.print("Temperature: ");
        // Serial.println(myData.nTemperature);
        // Serial.print("Humidity: ");
        // Serial.println(myData.nHumidity);
        // Serial.println();
    }
}
void ledlamp(byte red = 1, byte green = 1, byte blue = 1) {
    digitalWrite(LED_RED, red);
    digitalWrite(LED_BLUE, green);
    digitalWrite(LED_GREEN, blue);
}

// 偵測連線變化
void checkConnections() {
    int newConnections = 0;
    int disconnected = 0;
    int totalConnections = 0;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (connections[i].isConnected) {
            if (!esp_now_is_peer_exist(connections[i].macAddress)) {
                // 設備斷線
                Serial.println("Device disconnected");
                connections[i].isConnected = false;
                disconnected++;
            }
        } else {
            if (esp_now_is_peer_exist(connections[i].macAddress)) {
                // 新設備加入
                Serial.println("New device connected");
                connections[i].isConnected = true;
                newConnections++;
            }
        }
    }
    totalConnections = newConnections - disconnected;

    //  Serial.print("Total connected devices: ");
    //  Serial.println(connectedDevices);
}

#ifdef GY_86
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085);
#endif

bool timerchange = false;

void timerCallback() {
    timerchange = false;
    // timerAlarmEnable(Timer0_Cfg);
}
void setup() {
    Serial.begin(115200); /*serial init */
    delay(300);

#ifdef GY_86
    Wire.begin(SDA, SCL);
#endif

#ifdef JW01
    CO2Serial.begin(
        JW01_BAUD_RATE, EspSoftwareSerial::SWSERIAL_8N1, RX_PIN, TX_PIN);
#endif

    WiFi.mode(WIFI_STA);
    /*
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      while (WiFi.status() != WL_CONNECTED ) {
      delay(200);
      Serial.print(".") ;
      }
      Serial.println("" );
      Serial.println("WiFi connected.");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    */
    // 初始化OTA
    // ArduinoOTA.begin();
    // Serial.println("OTA Initialized");

    // 使用硬體計時器0建立1秒週期的計時器。
    Timer0_Cfg = timerBegin(0, 80, true);
    timerAttachInterrupt(Timer0_Cfg, &Timer0_ISR, true);
    timerAlarmWrite(Timer0_Cfg, 1000000, true);
    timerAlarmEnable(Timer0_Cfg);

    // INIT LED LIGHT
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    ledlamp();

    // LCD init
    lcd.begin();
    lcd.setRotation(0);
    lcd.fillScreen(TFT_BLACK);
    touch.setCal(526, 3443, 750, 3377, 320, 240, 1);
    touch.setRotation(0);

    delay(100);
    // background light pin
    pinMode(27, OUTPUT);
    digitalWrite(27, HIGH);

    // lvgl init
    lv_init();
    lv_disp_draw_buf_init(
        &draw_buf, buf1, NULL, screenWidth * screenHeight / 13);

    /*Display init*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    /*Display driver port of LVGL*/
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    /*touch driver port of LVGL*/
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    ui_init(); // LVGL UI init

    Serial.println("\nDispplay Setup done");
    // lv_timer_handler();
    // String Mac;
    // sprintf(Mac, WiFi.macAddress());
    lv_label_set_text(ui_Label7, WiFi.macAddress().c_str());
    lv_timer_handler();
    /*
      if (WiFi.status() != WL_CONNECTED) // if WiFi is not connected
      {
      //Init WiFi as Station, start SmartConfig

      WiFi.beginSmartConfig();

      //Wait for SmartConfig packet from mobile
      Serial.println("Waiting for SmartConfig.");
      while (!WiFi.smartConfigDone()) {
        delay(500);
        Serial.print(".");
      }
      Serial.println("");
      Serial.println("SmartConfig received.");

      //Wait for WiFi to connect to AP
      Serial.println("Waiting for WiFi");
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
      }
      Serial.println("WiFi Connected.");
      }
      else
      {
      Serial.println("WiFi Connected");
      }
    */
    // 初始化ESPNOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    // 初始化連線清單
    memset(connections, 0, sizeof(connections));
    esp_now_register_recv_cb(OnDataRecv);
    ledlamp(1, 0, 1);
    lv_label_set_text(ui_Wifi, Version);

#ifdef GY_86
    while (!bmp.begin()) {
        Serial.print(
            "Ooops, no BMP180 detected ... Check your wiring or I2C ADDR!");
        delay(300);
    }
#endif
    timer = timerBegin(1, 80, true); // 使用 Timer 1，分頻為 80
    timerAttachInterrupt(timer, &timerCallback, true); // 連接中斷服務函數
    timerAlarmWrite(timer, 30000000, true); // 設定計時器觸發時間為 30 秒
    timerAlarmEnable(timer);                // 啟用計時器
}

byte ledb = 0;
byte ledg = 1;
float lastVoltage = 0, lastTemperature = 0;
float highV = 0, lowV = 0, highT = 0, lowT = 0;

unsigned long lastLedMs = 0;
unsigned long lastSensorMs = 0;
unsigned long lastClockMs = 0;

void loop() {
    unsigned long nowMs = millis();

    // 1. LVGL UI 與觸控極速處理 (即時響應，無阻延)
    lv_timer_handler();

    // 2. LED 狀態指示燈 (每 500ms 刷新)
    if (nowMs - lastLedMs >= 500) {
        lastLedMs = nowMs;
        if (lastVoltage > 0 && lastVoltage < 12.5) { // 低電壓警示 (<12.5V 亮紅燈)
            ledlamp(0, 1, 1);
        } else {
            ledlamp(1, ledg, ledb);
            ledb = abs(ledb - 1);
            ledg = abs(ledg - 1);
        }
    }

#ifdef JW01
    // 3. JW01 空氣品質感測器數據接收 (非阻塞，滿 9 bytes 即解析)
    const int packetSize = 9;
    byte packet[packetSize];

    if (CO2Serial.available() >= packetSize) {
        for (int i = 0; i < packetSize; i++) {
            packet[i] = CO2Serial.read();
        }
        byte sum = 0;
        for (int i = 0; i < 8; i++) sum += packet[i];
        if (packet[8] == sum) {
            char TS[32];
            int CO2 = packet[6] * 256 + packet[7];
            sprintf(TS, "%d ppm", CO2);
            if (CO2 < 32767) {
                if (CO2 > 3500) {
                    lv_obj_set_style_bg_color(ui_CO2Bar, lv_color_hex(0xFF0000),
                        LV_PART_INDICATOR | LV_STATE_DEFAULT);
                } else {
                    lv_obj_set_style_bg_color(ui_CO2Bar, lv_color_hex(0x00FF00),
                        LV_PART_INDICATOR | LV_STATE_DEFAULT);
                }
                lv_label_set_text(ui_CO2Val, TS);
            }

            float TVOC = packet[2] * 256 + packet[3] * 0.001;
            sprintf(TS, "%0.4g mg/m3", TVOC);
            if (TVOC < 10) {
                lv_label_set_text(ui_TVOCVal, TS);
                if (TVOC > 0.6) {
                    lv_obj_set_style_bg_color(ui_TVOCBar,
                        lv_color_hex(0xFF0000),
                        LV_PART_INDICATOR | LV_STATE_DEFAULT);
                } else {
                    lv_obj_set_style_bg_color(ui_TVOCBar,
                        lv_color_hex(0x00FF00),
                        LV_PART_INDICATOR | LV_STATE_DEFAULT);
                }
            }

            float CH20 = packet[4] * 256 + packet[5] * 0.001;
            sprintf(TS, "%0.4g mg/m3", CH20);
            if (CH20 < 10) {
                lv_label_set_text(ui_CH20Val, TS);
                if (CH20 > 0.15) {
                    lv_obj_set_style_bg_color(ui_CH20Bar,
                        lv_color_hex(0xFF0000),
                        LV_PART_INDICATOR | LV_STATE_DEFAULT);
                } else {
                    lv_obj_set_style_bg_color(ui_CH20Bar,
                        lv_color_hex(0x00FF00),
                        LV_PART_INDICATOR | LV_STATE_DEFAULT);
                }
            }
        }
    }
#endif

    // 4. 時鐘與遙測數據刷新 (每 200ms)
    if (nowMs - lastClockMs >= 200) {
        lastClockMs = nowMs;
        char TS[16];
        sprintf(TS, "%02d:%02d:%02d", Hr, Min, Sec);
        lv_label_set_text(ui_Label9, TS);

        if (rec_success) {
            char s[16];
            if (lastVoltage != myData.nVoltage) {
                sprintf(s, "%.1f V", myData.nVoltage);
                lastVoltage = myData.nVoltage;
                lv_label_set_text(ui_Label1, s);
                if (lastVoltage > highV) {
                    highV = lastVoltage;
                    sprintf(s, "%.1f V", highV);
                    lv_label_set_text(ui_HighV, s);
                }
                if (lastVoltage < lowV || lowV == 0) {
                    lowV = lastVoltage;
                    sprintf(s, "%.1f V", lowV);
                    lv_label_set_text(ui_LowV, s);
                }
            }

            if (lastTemperature != myData.nTemperature) {
                sprintf(s, "%.1f °C", myData.nTemperature);
                lastTemperature = myData.nTemperature;
                lv_label_set_text(ui_Label3, s);
                if (lastTemperature > highT) {
                    highT = lastTemperature;
                    sprintf(s, "%.1f °C", highT);
                    lv_label_set_text(ui_HighT, s);
                }
                if (lastTemperature < lowT || lowT == 0) {
                    lowT = lastTemperature;
                    sprintf(s, "%.1f °C", lowT);
                    lv_label_set_text(ui_LowT, s);
                }
            }

            lv_label_set_text(ui_CQTY, String(connectedDevices).c_str());
            lv_label_set_text(ui_RecordCount, String(RecordCount).c_str());
            rec_success = false;
        }
    }

#ifdef GY_86
    // 5. GY-86 氣壓與海拔高度感測 (每 1000ms)
    if (nowMs - lastSensorMs >= 1000) {
        lastSensorMs = nowMs;
        sensors_event_t event;
        bmp.getEvent(&event);

        if (event.pressure) {
            float rawPressure = event.pressure;
            static float smoothPressure = 0.0f;
            if (smoothPressure <= 0.0f) {
                smoothPressure = rawPressure;
            } else {
                smoothPressure = smoothPressure * 0.7f + rawPressure * 0.3f;
            }

            // 讀取 BMP180 現場環境溫度以進行高精度氣壓高度熱力學補償
            float tempC = 25.0f;
            bmp.getTemperature(&tempC);

            // 國際標準大氣(ISA)結合現場真實溫度之氣壓測高公式：
            // h = ((P0 / P)^(1/5.255) - 1.0) * (T_celsius + 273.15) / 0.0065
            float altitude = ((powf(SEALEVELPRESSURE_HPA / smoothPressure, 0.1902949f) - 1.0f) * (tempC + 273.15f)) / 0.0065f;

            char TS[32];
            sprintf(TS, "%04.2f hPa", smoothPressure);
            lv_label_set_text(ui_Pvalue, TS);
            sprintf(TS, "%04.2f m", altitude);
            lv_label_set_text(ui_Avalue, TS);
        }
    }
#endif

    // 6. 30 秒自動輪播控制 (Timer 1 觸發)
    if (!timerchange) {
        timerchange = true;
        Screen = abs(Screen - 1);
        if ((Screen > 0) && (lastVoltage > 0)) {
            _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0,
                &ui_Screen1_screen_init);
        } else {
            _ui_screen_change(&ui_Screen2, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0,
                &ui_Screen2_screen_init);
        }
    }

    // 每次 loop 微幅讓出時間給底層任務，並保持 200Hz 觸控與刷新更新頻率
    delay(5);
}