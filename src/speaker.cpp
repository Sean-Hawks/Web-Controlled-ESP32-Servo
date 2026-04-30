#include "notes.h"
#include "song_data.h"
#include <Arduino.h>
#include <OneButton.h>

// ----------------------
// Hardware config
// ----------------------
static const int BUZZER_PIN = 15; // 蜂鳴器接 GPIO 15
static const int LED_PIN = 2;     // 內建 LED 通常接 GPIO 2
static const int BUTTON_PIN = 5;  // 播放/暫停 GPIO 5
static const int BUTTON_FF_PIN = 18; // 快進 GPIO 18
static const int BUTTON_RW_PIN = 19; // 倒退 GPIO 19

static const int LEDC_CH = 0;
// 修改 Resolution 為 8 bits (0-255)，通常對高頻音效支援度較好
static const int LEDC_RES_BITS = 8; 

static const int MAX_DUTY = (1 << LEDC_RES_BITS) - 1; // 255
static const int DUTY_50 = MAX_DUTY / 2;              // ~127 (50% duty)

// ----------------------
OneButton button(BUTTON_PIN, true);
OneButton btnFF(BUTTON_FF_PIN, true);
OneButton btnRW(BUTTON_RW_PIN, true);

// ----------------------
// State machine
// ----------------------
enum class PlayerState { STOPPED,
                         PLAYING,
                         PAUSED };
static PlayerState state = PlayerState::STOPPED;

// playback
static int idx = 0;
static unsigned long nextChangeAt = 0;

// ----------------------
// Safe tone helpers
// ----------------------
static inline void buzzerOff() {
    ledcWrite(LEDC_CH, 0);
}

static inline void buzzerOn(int freq) {
    if (freq == NOTE_REST) {
        buzzerOff();
        return;
    }
    ::ledcWriteTone(LEDC_CH, freq);
    ledcWrite(LEDC_CH, DUTY_50);
}

// ----------------------
// State transitions
// ----------------------
static void stopMusicOutput() {
    state = PlayerState::PAUSED;
    buzzerOff();
    nextChangeAt = 0;
    
    // 當音樂停止播放時，LED指示燈亮 (停止閃爍，保持恆亮)
    digitalWrite(LED_PIN, HIGH);
}

static void pausePlay() {
    // 暫停播放 (不重置 idx)
    stopMusicOutput();
    Serial.println("State: PAUSED");
}

static void resetPlay() {
    // 重置播放 (idx = 0) 並停止
    stopMusicOutput();
    idx = 0; 
    Serial.println("State: RESET (Stopped at start)");
}

static void startPlay() {
    state = PlayerState::PLAYING;
    
    // 如果已經播完了，從頭開始
    if (idx >= total_notes) idx = 0; 
    
    nextChangeAt = 0; // play immediately
    Serial.println("State: PLAYING");
}

static void togglePlay() {
    if (state == PlayerState::PLAYING) {
        pausePlay(); // 短按暫停
    } else {
        startPlay(); // 短按繼續
    }
}

// ----------------------
// Time Jump Logic
// ----------------------
static void jumpTime(int ms) {
    int current_ms = 0;
    int target_ms = abs(ms);
    int direction = (ms > 0) ? 1 : -1;
    
    int new_idx = idx;
    
    // 如果是往回，且剛好在開頭，就不動
    if (direction == -1 && new_idx <= 0) return;
    
    // 如果是快進，且已經結束，就不動
    if (direction == 1 && new_idx >= total_notes) return;

    while (current_ms < target_ms) {
        if (direction == 1) {
            // Forward
            if (new_idx >= total_notes - 1) {
                new_idx = total_notes;
                break;
            }
            current_ms += duration_ms[new_idx];
            new_idx++;
        } else {
            // Backward
            if (new_idx <= 0) {
                new_idx = 0;
                break;
            }
            new_idx--; 
            current_ms += duration_ms[new_idx]; // Note: using duration of previous note
        }
    }
    
    idx = new_idx;
    nextChangeAt = 0; // Play immediately if playing (to sync rhythm)
    
    Serial.printf("Jumped %d ms, new idx: %d\n", ms, idx);
    
    // 如果暫停中，跳轉後還是暫停，但位置變了
    // 如果播放中，跳轉後會立即播放新位置的音符
}

// ----------------------
// Button Handlers
// ----------------------
static void handleClick() {
    Serial.println("Button Clicked");
    togglePlay();
}

static void handleLongPress() {
    Serial.println("Button Long Pressed");
    // 長按按鈕開關，重頭播放並暫停
    resetPlay();
}

static void handleFF() {
    Serial.println("Fast Forward 5s");
    jumpTime(5000);
}

static void handleRW() {
    Serial.println("Rewind 5s");
    jumpTime(-5000);
}

// ----------------------
// Player tick (non-blocking)
// ----------------------
static void playerTick() {
    if (state != PlayerState::PLAYING)
        return;

    unsigned long now = millis();
    if (nextChangeAt != 0 && now < nextChangeAt)
        return;

    if (idx >= total_notes) {
        resetPlay(); // Auto-stop at end (reset to 0)
        return;
    }

    int freq = melody[idx];
    int d = duration_ms[idx];
    if (d < 1) d = 1;

    buzzerOn(freq);

    // 當音樂正在播放時，LED指示燈會閃爍
    // Toggle LED state
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));

    nextChangeAt = now + (unsigned long)d + (unsigned long)note_gap_ms;
    idx++;
}

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Audio Player Starting...");
    
    // LED setup
    pinMode(LED_PIN, OUTPUT);
    
    // LEDC init
    ledcSetup(LEDC_CH, 2000, LEDC_RES_BITS);
    ledcAttachPin(BUZZER_PIN, LEDC_CH);
    buzzerOff();

    // Button Setup
    button.attachClick(handleClick);
    button.attachLongPressStart(handleLongPress);
    btnFF.attachClick(handleFF);
    btnRW.attachClick(handleRW);
    
    // Initial State: Music stopped, LED ON
    resetPlay();
    
    // Power-on beep test
    Serial.println("Testing buzzer...");
    ledcWriteTone(LEDC_CH, 1000);
    ledcWrite(LEDC_CH, DUTY_50);
    delay(200);
    buzzerOff();
    Serial.println("Buzzer test done.");
    
    Serial.println("Boot ready. Waiting for button press...");
}

void loop() {
    // Keep watching the push buttons
    button.tick();
    btnFF.tick();
    btnRW.tick();

    // Handle music playback
    playerTick();
}
