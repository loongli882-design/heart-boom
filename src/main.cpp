#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MAX30105.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define I2C_SDA 21
#define I2C_SCL 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
MAX30105 particleSensor;

// ================= 极简参数配置 =================
const int FILTER_SIZE = 5;            // 轻度滑动平均窗口（5个点，刚好抹平毛刺）
const int MIN_BEAT_INTERVAL = 500;    // 不应期500ms（最高允许120BPM），防止双倍误判
const int BPM_WINDOW_SIZE = 5;        // ⚠️ 新增：心率滑动窗口大小（存最近5次心跳）

// ================= 滤波器变量 =================
long irBuffer[FILTER_SIZE];
int bufferIndex = 0;
long irSum = 0;
float filteredIR = 0;                 // 滤波后的IR
float slowBaseline = 0;               // 缓慢移动的基线（地平线）
bool isAboveThreshold = false;        // 状态机标志位（记录当前是否在地平线之上）

// ================= 心率变量 =================
float currentBPM = 0;
unsigned long lastBeatTime = 0;
unsigned long lastDisplay = 0;

// ⚠️ 新增：心率滑动窗口缓冲区
float bpmWindow[BPM_WINDOW_SIZE] = {0};
int bpmIndex = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED not found!");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Simple Algo V2...");
    display.display();
  }

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found!");
    while (1); 
  }
  
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0x1F);
  particleSensor.setPulseAmplitudeIR(0x1F);
  particleSensor.setSampleRate(100); // 100Hz采样率

  // 预填充滤波器，防开机剧烈跳动
  for(int i = 0; i < FILTER_SIZE; i++) {
    irBuffer[i] = particleSensor.getIR();
    irSum += irBuffer[i];
    delay(10);
  }
  filteredIR = irSum / FILTER_SIZE;
  slowBaseline = filteredIR;
}

void loop() {
  long rawIR = particleSensor.getIR();

  // 1. 滑动平均滤波（轻度去噪）
  irSum -= irBuffer[bufferIndex];
  irBuffer[bufferIndex] = rawIR;
  irSum += rawIR;
  bufferIndex = (bufferIndex + 1) % FILTER_SIZE;
  filteredIR = (float)irSum / FILTER_SIZE;

  // 2. 缓慢追踪基线（地平线，系数0.01表示跟随极慢，忽略心跳波动）
  slowBaseline = (0.99 * slowBaseline) + (0.01 * filteredIR);

  // 3. 极简波峰检测（信号向上穿越基线）
  if (rawIR > 8000) { // 有手指的前提下
    if (filteredIR > slowBaseline && !isAboveThreshold) {
      isAboveThreshold = true;
      
      unsigned long currentTime = millis();
      if (currentTime - lastBeatTime > MIN_BEAT_INTERVAL) {
        float interval = (currentTime - lastBeatTime) / 1000.0;
        float instantBPM = 60.0 / interval;

        // 生理限制过滤（防止乱算）
        if (instantBPM > 45.0 && instantBPM < 120.0) {
          // ⚠️ 修改：不再使用迟钝的加权平均，改用滑动窗口平均
          bpmWindow[bpmIndex] = instantBPM;
          bpmIndex = (bpmIndex + 1) % BPM_WINDOW_SIZE;
          
          float sum = 0;
          int count = 0;
          for(int i = 0; i < BPM_WINDOW_SIZE; i++) {
            if(bpmWindow[i] > 0) {
              sum += bpmWindow[i];
              count++;
            }
          }
          if(count > 0) {
            currentBPM = sum / count; // 瞬间响应，同时保持稳定
          }
        }
        lastBeatTime = currentTime;
      }
    } else if (filteredIR < slowBaseline) {
      isAboveThreshold = false;
    }
  } else {
    // 没手指时重置
    currentBPM = 0;
    lastBeatTime = 0;
    isAboveThreshold = false;
    for(int i = 0; i < BPM_WINDOW_SIZE; i++) bpmWindow[i] = 0; // 清空滑动窗口
  }

  // 4. 发给 Teleplot 画图
  Serial.printf(">RawIR:%ld\n", rawIR);
  Serial.printf(">FilteredIR:%f\n", filteredIR);
  Serial.printf(">Baseline:%f\n", slowBaseline);
  Serial.printf(">BPM:%f\n", currentBPM);

  // 5. 刷新 OLED 屏幕
  if (millis() - lastDisplay > 200) {
    lastDisplay = millis();
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("IR: ");
    display.println((long)filteredIR);
    display.print("BPM: ");
    display.println(currentBPM, 1);
    display.display();
  }
}