#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "web_pages.h"
#include <ESP32Servo.h>
#include <IRremote.h>

// --- 實體馬達控制區 ---
Servo myServo;
bool motorRunning = false;
int currentAngle = 0;   
int rotationDir = 1;
int motorBPM = 60;  // 預設速度：60 BPM (每分鐘 60 拍 = 每秒 1 拍)
const int IR_RECEIVE_PIN = 14;

// 為馬達建立專屬的 FreeRTOS 任務代號
TaskHandle_t MotorTaskHandle;

// 全域宣告 WebSocket，打通網頁即時串流雙向通道
AsyncWebSocket ws("/ws");

void initMotor() {
    // 關鍵設定：強制分配獨立的硬體計時器給馬達，避免跟 WiFi 及 IR 搶資源
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    myServo.setPeriodHertz(50); // 標準 50Hz
    myServo.attach(13, 500, 2400); 
    myServo.write(currentAngle);
    Serial.println("Real Motor Initialized!");
}

void motorTick() {
    // 記錄時間差 (Delta Time) 可以讓動畫不受「程式卡頓」影響
    static unsigned long lastTick = millis();
    unsigned long now = millis();
    unsigned long dt = now - lastTick;
    lastTick = now;

    if (motorRunning) {
        // 【引入工業級「正弦波緩起停」算法 (Sine Wave Ease-in/Out) 】
        // 解決高速時發生「瞬移、卡頓」的問題
        static float phase = 0.0;
        
        // 計算角速度：BPM = 每分鐘拍數 (1拍 = 跨越 180 度 = PI 的相位)
        float radPerSec = (motorBPM * PI) / 60.0;
        float radPerMs = radPerSec / 1000.0;

        // 將時間乘上速度推動相位，支援正反轉
        phase += rotationDir * (radPerMs * dt);

        // 保持相位在 0 到 2*PI 的迴圈內
        while (phase >= 2 * PI) phase -= 2 * PI;
        while (phase < 0.0) phase += 2 * PI;

        // 運用 Cosine 波形計算角度： 
        // 當 phase=0度 時 -> 取值為 0
        // 當 phase=180度(PI) 時 -> 取值為 180
        // 這樣在 0 度跟 180 的兩端，速度會自動「完美緩減速與緩起步」
        float exactAngle = 90.0 - 90.0 * cos(phase);

        currentAngle = (int)exactAngle;

        // 用 15 毫秒的高刷新率餵給馬達 (約66FPS的流暢度)
        static unsigned long lastServoUpdate = 0;
        if (now - lastServoUpdate >= 15) {
            lastServoUpdate = now;
            myServo.write(currentAngle);
        }
    }

    // ✅ 【WebSockets 零延遲推播 - 智慧防塞車版防斷線】
    static unsigned long lastWsSync = 0;
    static bool lastSentState = false;
    
    // 1. 如果切換 START/STOP 狀態，強制發送一次更新
    bool stateChanged = (motorRunning != lastSentState);
    // 2. 運行中每 50 毫秒(20FPS) 發送一次 (配合網頁 css transition 0.05s 剛好)
    bool timeToUpdate = (motorRunning && (now - lastWsSync >= 50));

    if (stateChanged || timeToUpdate) {
        lastWsSync = now;
        lastSentState = motorRunning;

        if (ws.count() > 0) { 
            String json = "{\"angle\": " + String(currentAngle) + ",";
            json += "\"state\": \"" + String(motorRunning ? "RUNNING" : "STOP") + "\",";
            json += "\"bpm\": " + String(motorBPM) + "}";
            
            // 安全巡檢：只把封包派塞給「目前沒有塞車、活著」的連線，避開 Too many messages queued
            for (auto& client : ws.getClients()) {
                if (client.status() == WS_CONNECTED && client.canSend()) {
                    client.text(json);
                }
            }
            ws.cleanupClients();
        }
    }
}

// 這是要在另一個核心上獨立運行的程式碼
void MotorTask(void * pvParameters) {
    for (;;) {
        motorTick();
        vTaskDelay(1); // 讓出 CPU 1毫秒給其他的系統工作
    }
}

void updateLCD() {
    // 如果你還沒接實體 LCD，這裡可以先保留 Serial 輸出
    Serial.printf("Motor State: Running=%d, Dir=%d\n", motorRunning, rotationDir);
}
// ----------------------------------------------

AsyncWebServer server(80);
const char* ssid = "Euler";
const char* password = "2.7182818284590452353";

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\nConnecting to WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        attempt++;
        Serial.printf("Attempt %d: Status %d\n", attempt, WiFi.status());
        if (attempt > 20) { ESP.restart(); }
    }

    initMotor();

    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // --- 重點開始：設定網頁路由 ---
    
    // 1. 首頁：回傳漂亮的高級儀表板
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println(">>> 偵測到首頁訪問！");
        request->send(200, "text/html", index_html);
    });

    // 2. 切換啟動/停止
    server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request){
        motorRunning = !motorRunning;
        updateLCD();
        Serial.printf(">>> Motor State Toggled: %s\n", motorRunning ? "RUN" : "STOP");
        request->send(200, "text/plain", motorRunning ? "Running" : "Stopped");
    });

    // 3. 切換方向 (我幫你把 /changeDir 補上了，對應網頁上的 REVERSE 按鈕)
    server.on("/changeDir", HTTP_GET, [](AsyncWebServerRequest *request){
        rotationDir *= -1;
        updateLCD();
        request->send(200, "text/plain", "Direction Changed");
    });

    // 4. 設定速度 BPM (處理從網頁拖動滑桿傳來的數值)
    server.on("/setBPM", HTTP_GET, [](AsyncWebServerRequest *request){
        if(request->hasParam("value")){
            motorBPM = request->getParam("value")->value().toInt();
            // 防止 BPM 為 0 導致除以 0 的錯誤
            if(motorBPM < 10) motorBPM = 10; 
            if(motorBPM > 300) motorBPM = 300;
            Serial.printf(">>> BPM changed to: %d\n", motorBPM);
        }
        request->send(200, "text/plain", String(motorBPM));
    });

    // 5. 獲取完整狀態 (包含角度與啟動/停止的狀態 - 解決 STATUS 不同步問題)
    server.on("/getStatus", HTTP_GET, [](AsyncWebServerRequest *request){
        // 回傳一段 JSON 格式的資料給網頁
        String json = "{\"angle\": " + String(currentAngle) + ",";
        json += "\"state\": \"" + String(motorRunning ? "RUNNING" : "STOP") + "\",";
        json += "\"bpm\": " + String(motorBPM) + "}";
        
        request->send(200, "application/json", json);
    });

    // 6. 緊急停止
    server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request){
        motorRunning = false;
        updateLCD();
        request->send(200, "text/plain", "Emergency Stopped");
    });

    // 掛載 WebSocket 到網頁伺服器
    server.addHandler(&ws);

    // 啟動伺服器
    server.begin();
    Serial.println("HTTP server started!");

    IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
    Serial.println("[System] IR Receiver Ready!");

    // 【開大絕】把馬達控制丟給雙核心的 ESP32 的另一個 CPU 核心 (Core 1)
    // 這樣 WiFi 就算在背景狂運算 (Core 0)，也影響不到馬達了！
    xTaskCreatePinnedToCore(
        MotorTask,         // 任務名稱
        "MotorTask",       // 識別名稱
        4096,              // 記憶體大小
        NULL,              // 傳遞參數
        1,                 // 優先級 (數字越大優先級越高)
        &MotorTaskHandle,  // 任務卡號
        1                  // 綁定在 Core 1 (App Core) 上執行
    );
}

void loop() {
    // 原本的 motorTick 已經搬去另一個 CPU 核心了！
    // 這裡 (Core 1 預設環境) 只剩下「輕鬆」的工作，例如等紅外線遙控器。

    // 👉 4. 偵測是否有收到紅外線訊號
    if (IrReceiver.decode()) {
        unsigned long irCode = IrReceiver.decodedIRData.decodedRawData;
        
        // 忽略 0x0 的無效訊號或重複碼雜訊
        if (irCode != 0) {
            // 印出你按下的按鍵代碼 (很重要！你要看著 Serial Monitor 抄下這些代碼)
            Serial.printf("收到紅外線代碼: 0x%lX\n", irCode);

            // 判斷是哪個按鍵，並執行對應動作
            // 注意：這裡的 0x... 每個人的遙控器都不一樣，你需要替換成你測出來的代碼！
            if (irCode == 0xBC43FF00) { 
                // 假設這是「播放/暫停」鍵
                motorRunning = !motorRunning;
                updateLCD();
                Serial.println("IR Command: Toggle Motor");
            } 
            else if (irCode == 0xF609FF00) { 
                // 假設這是「換向」鍵
                rotationDir *= -1;
                updateLCD();
                Serial.println("IR Command: Change Direction");
            }
        }

        // 準備接收下一個訊號
        IrReceiver.resume(); 
    }
}