# include \<FastLED.h\>

#define uchar unsigned char
#define RGB_PIN   2         // RGB灯使用IO2进行控制
#define NUM_LEDS  3         // RGB灯的数量
CRGB leds[NUM_LEDS];       // CRGB是结构体类型

// 呼吸效果参数
const int BREATH_STEPS = 50;  // 呼吸渐变步数
const int BREATH_DELAY = 20;  // 每步延时(ms)
float breathBrightness = 0;   // 当前亮度
float breathDelta = 1.0;      // 亮度变化方向

// 定义颜色结构体
struct Color {
  uchar r;
  uchar g;
  uchar b;
};

void RGB_Init(void) {
  FastLED.addLeds<WS2812, RGB_PIN, GRB>(leds, NUM_LEDS);
}

// 带亮度控制的RGB灯控制函数
void RGB_Control(uchar cresset, uchar red, uchar green, uchar blue, float brightness) {
  leds[cresset] = CRGB(
    red * brightness,
    green * brightness,
    blue * brightness
  );
  FastLED.show();
}

// 生成随机颜色
Color getRandomColor() {
  Color c;
  // 随机选择一个主色调
  switch(random(6)) {
    case 0:
      c = {255, 0, 0};    // 红
      break;
    case 1:
      c = {0, 255, 0};    // 绿
      break;
    case 2:
      c = {0, 0, 255};    // 蓝
      break;
    case 3:
      c = {255, 255, 0};  // 黄
      break;
    case 4:
      c = {255, 0, 255};  // 紫
      break;
    case 5:
      c = {0, 255, 255};  // 青
      break;
  }
  return c;
}

void setup() {
  Serial.begin(115200);
  RGB_Init();
  randomSeed(analogRead(0)); // 初始化随机数生成器
  
  // 设置输出引脚
  for(int i = 3; i <= 13; i++) {
    pinMode(i, OUTPUT);
    digitalWrite(i, LOW);
  }
}

void loop() {
  // 为每个LED生成随机颜色
  Color colors[NUM_LEDS];
  for(int i = 0; i < NUM_LEDS; i++) {
    colors[i] = getRandomColor();
  }
  
  // 呼吸效果循环
  for(int step = 0; step < BREATH_STEPS; step++) {
    // 更新呼吸亮度
    breathBrightness += breathDelta * (1.0 / BREATH_STEPS);
    
    // 在到达最大或最小亮度时改变方向
    if(breathBrightness >= 1.0) {
      breathBrightness = 1.0;
      breathDelta = -1.0;
    } else if(breathBrightness <= 0.0) {
      breathBrightness = 0.0;
      breathDelta = 1.0;
      // 在完成一次呼吸周期后重新生成随机颜色
      for(int i = 0; i < NUM_LEDS; i++) {
        colors[i] = getRandomColor();
      }
    }
    
    // 更新所有LED
    for(int i = 0; i < NUM_LEDS; i++) {
      RGB_Control(i, 
        colors[i].r, 
        colors[i].g, 
        colors[i].b, 
        breathBrightness
      );
    }
    
    delay(BREATH_DELAY);
  }
  
  // 随机延时，增加闪烁的不规则性
  delay(random(100, 500));
}
