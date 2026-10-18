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

// ================= 参数配置 =================
const int FILTER_SIZE = 10;           
const int MIN_BEAT_INTERVAL = 400;    

// ⚠️ 修改：将滑动窗口从 3 放宽到 5（如果你想要更稳，可以改成 6）
// 配合下面的突变锁，既能保证平滑，又能快速响应
const int BPM_WINDOW_SIZE = 5;        

const float MAX_BPM_JUMP = 10.0;       
int rejectCount = 0; 

long irBuffer[FILTER_SIZE];
int bufferIndex = 0;
long irSum = 0;
float filteredIR = 0;                 
float slowBaseline = 0;               
float lastAC = 0;                     
float dynamicThreshold = 100;         

float currentBPM = 0;
unsigned long lastBeatTime = 0;
unsigned long lastDisplay = 0;

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
    display.println("Algorithm V8...");
    display.display();
  }

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found!");
    while (1); 
  }
  
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0x1F);
  particleSensor.setPulseAmplitudeIR(0x1F);
  particleSensor.setSampleRate(100); 

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

  // 1. 滑动平均滤波
  irSum -= irBuffer[bufferIndex];
  irBuffer[bufferIndex] = rawIR;
  irSum += rawIR;
  bufferIndex = (bufferIndex + 1) % FILTER_SIZE;
  filteredIR = (float)irSum / FILTER_SIZE;

  // 2. 基线追踪与交流分量提取
  slowBaseline = (0.90 * slowBaseline) + (0.10 * filteredIR);
  float acSignal = filteredIR - slowBaseline;
  float slope = acSignal - lastAC;
  lastAC = acSignal;

  // 3. 动态阈值
  if (acSignal > dynamicThreshold) {
    dynamicThreshold = (0.8 * dynamicThreshold) + (0.2 * acSignal); 
  } else {
    dynamicThreshold = (0.99 * dynamicThreshold) + (0.01 * acSignal);
  }
  if (dynamicThreshold < 30) dynamicThreshold = 30; 

  // 4. 波峰检测
  if (rawIR > 8000) { 
    if (slope < 0 && acSignal > dynamicThreshold && (millis() - lastBeatTime > MIN_BEAT_INTERVAL)) {
      unsigned long currentTime = millis();
      float interval = (currentTime - lastBeatTime) / 1000.0;
      
      if (lastBeatTime != 0) { 
        float instantBPM = 60.0 / interval;
        
        if (instantBPM > 40.0 && instantBPM < 150.0) { 
          
          if (currentBPM == 0 || abs(instantBPM - currentBPM) <= MAX_BPM_JUMP) {
            rejectCount = 0; 
            
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
              currentBPM = sum / count; 
            }
          } else {
            rejectCount++;
            Serial.printf("Jump Rejected (%d/5): %.2f -> %.2f\n", rejectCount, currentBPM, instantBPM);
            
            if (rejectCount >= 5) {
              Serial.println("Force Reset BPM due to continuous rejection!");
              currentBPM = 0; 
              rejectCount = 0;
              for(int i = 0; i < BPM_WINDOW_SIZE; i++) bpmWindow[i] = 0; 
            }
          }
        }
      }
      lastBeatTime = currentTime;
    }
  } else {
    // 没手指时重置
    currentBPM = 0;
    rejectCount = 0;
    lastBeatTime = 0;
    dynamicThreshold = 100;
    for(int i = 0; i < BPM_WINDOW_SIZE; i++) bpmWindow[i] = 0; 
  }

  // 5. 发给 Teleplot
  Serial.printf(">RawIR:%ld\n", rawIR);
  Serial.printf(">FilteredIR:%f\n", filteredIR);
  Serial.printf(">Baseline:%f\n", slowBaseline);
  Serial.printf(">BPM:%f\n", currentBPM);

  // 6. 刷新 OLED
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