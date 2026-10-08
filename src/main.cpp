#include <Wire.h>                 // I2C通信库（连接屏幕和传感器）
#include <Adafruit_GFX.h>         // 屏幕绘图核心库
#include <Adafruit_SSD1306.h>     // OLED屏幕驱动
#include <MAX30105.h>             // MAX30102传感器驱动

// --- 硬件参数配置 ---
#define SCREEN_WIDTH 128          // 屏幕宽度（像素）
#define SCREEN_HEIGHT 64          // 屏幕高度（像素）
#define OLED_RESET -1             // 复位引脚（-1表示共享ESP32复位）
#define SCREEN_ADDRESS 0x3C       // OLED的I2C地址（一般是0x3C或0x3D）
#define I2C_SDA 21                // 数据线引脚
#define I2C_SCL 22                // 时钟线引脚

// --- 创建对象 ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
MAX30105 particleSensor;

// --- 全局变量 ---
bool oledOK = false;              // 记录OLED是否正常
bool maxOK = false;               // 记录传感器是否正常
long irValue = 0;                 // 红外光数据
long redValue = 0;                // 红光数据
unsigned long lastDisplay = 0;    // 记录上次刷新屏幕的时间

void setup() {
  Serial.begin(115200);           // 开启串口，波特率115200（对应Teleplot）
  delay(1000);                    // 等待串口稳定

  Wire.begin(I2C_SDA, I2C_SCL);   // 初始化I2C总线
  Wire.setClock(400000);          // 设置I2C速率（400kHz）

  // 初始化OLED
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  if (!oledOK) Serial.println("OLED not found!");

  // 初始化MAX30102传感器
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found!");
    maxOK = false;
  } else {
    maxOK = true;
    particleSensor.setup();
    // 关键：把红光和红外LED亮度开到最大，保证信号强度
    particleSensor.setPulseAmplitudeRed(0x1F);
    particleSensor.setPulseAmplitudeIR(0x1F);
    
    // 屏幕显示就绪提示
    if (oledOK) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("Ready. Place finger.");
      display.display();
    }
  }
}

void loop() {
  if (!maxOK) return;

  // 1. 读取传感器数据
  redValue = particleSensor.getRed();
  irValue = particleSensor.getIR();

  // 2. 发送给Teleplot画图
  // 格式必须为 ">变量名:%数值\n"，这是Teleplot的专属暗号
  Serial.printf(">Red:%ld\n", redValue);
  Serial.printf(">IR:%ld\n", irValue);

  // 3. 刷新OLED屏幕（每200ms一次，避免刷屏太快）
  // 这里不用delay()，否则会导致Teleplot波形卡顿
  if (millis() - lastDisplay > 200) {
    lastDisplay = millis();
    if (oledOK) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.print("Red: ");
      display.println(redValue);
      display.print("IR: ");
      display.println(irValue);
      display.display();
    }
  }
}