#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>

// 定義硬體
Servo myServo;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// 作業要求的狀態變數 (不加 static，讓外部可見)
bool motorRunning = false;    // Stop / Running
int currentAngle = 0;         // 0 ~ 180
int rotationDir = 1;          // 1: C.W., -1: C.C.W.
unsigned long lastMoveTime = 0;

void initMotor() {
    myServo.attach(13); // 請根據你的接線修改
    lcd.init();
    lcd.backlight();
    myServo.write(0);
    // 初始 LCD 顯示
    lcd.setCursor(0, 0); lcd.print("Stop    0 deg");
    lcd.setCursor(0, 1); lcd.print("C.W.           ");
}

// 更新 LCD 的函數 (隨時被呼叫)
void updateLCD() {
    lcd.setCursor(0, 0);
    lcd.print(motorRunning ? "Running " : "Stop    ");
    lcd.setCursor(8, 0);
    lcd.print(String(currentAngle) + " deg  ");
    
    lcd.setCursor(0, 1);
    lcd.print(rotationDir == 1 ? "C.W.  " : "C.C.W.");
}

// 馬達的核心運作邏輯
void motorTick() {
    if (!motorRunning) return;

    if (millis() - lastMoveTime >= 300) { // 作業要求：每 0.3 秒
        lastMoveTime = millis();
        currentAngle += (rotationDir * 5); // 作業要求：移動 5 度

        // 往復循環邏輯
        if (currentAngle >= 180) { 
            currentAngle = 180; 
            rotationDir = -1; 
        } else if (currentAngle <= 0) { 
            currentAngle = 0; 
            rotationDir = 1; 
        }

        myServo.write(currentAngle);
        updateLCD(); // 即時更新 LCD
    }
}