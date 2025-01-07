// HPE Gen10 Plus fan proxy for Noctua NF-A8 PWM fan
#include <FastLED.h>

/* 定义引脚相关的宏 */
#define pinOfPin(P) (((P) >= 0 && (P) < 8) ? &PIND : (((P) > 7 && (P) < 14) ? &PINB : &PINC))
#define pinIndex(P) ((uint8_t)(P > 13 ? P - 14 : P & 7))
#define pinMask(P) ((uint8_t)(1 << pinIndex(P)))
#define isHigh(P) ((*(pinOfPin(P)) & pinMask(P)) > 0) /* 判断引脚是否为高电平 */
#define isLow(P) ((*(pinOfPin(P)) & pinMask(P)) == 0) /* 判断引脚是否为低电平 */

/* 引脚和常量定义 */
const int pwmInPin = A1;     /* 输入PWM信号的引脚 */
const int pwmOutPin = 9;     /* 输出PWM信号的引脚 */
const int pwm2OutPin = 10;   /* 第二个输出PWM信号的引脚 */
const int rpmInPin = 4;      /* RPM输入引脚 */
const int hpeTachPin = A2;   /* HPE转速信号输出引脚 */
const int normalTachPin = 3; /* 正常转速信号输出引脚 */
const uint16_t pwmTop = 320; /* PWM的顶值 */
const int sample = 25000;    /* 读取PWM的样本数量 */
const float pwmMap[4] = {0.1, 0.2, 0.5, 1.0}; /* PWM映射值 */

const int rpmUpper = 6000;                    /* 最大RPM */
const int rpmLower = 60;                      /* 最小RPM */
const float timer2Freq = 16000000 / 1024 / 2; /* Timer2频率 */
const int timer2RpmLower = 900;               /* Timer2最低RPM */
const float pwm2OutDutty = 0.45;              /* 第二个PWM输出的占空比 */

volatile uint32_t pulseCount = 0; /* 脉冲计数，使用volatile确保在中断中安全更新 */

/* 脉冲计数函数 */
void counter() { pulseCount++; }

/* 中断服务例程，处理RPM输入引脚的中断 */
ISR(PCINT2_vect) {
  if (digitalRead(rpmInPin) == HIGH) /* 检查引脚状态 */
    pulseCount++;                    /* 增加脉冲计数 */
}

void setup() {
  /* 配置Timer 1以产生25 kHz的PWM信号 */
  TCCR1A = 0;                                                                     /* 重置配置 */
  TCCR1B = 0;                                                                     /* 重置配置 */
  TCNT1 = 0;                                                                      /* 重置计数器 */
  TCCR1A = _BV(COM1A1) /* 非反向PWM信号 */ | _BV(COM1B1) /* 同上 */ | _BV(WGM11); /* 模式10：相位校正PWM，TOP = ICR1 */
  TCCR1B = _BV(WGM13) /* 同上 */ | _BV(CS10);                                     /* 预分频器 = 1 */
  ICR1 = pwmTop;                                                                  /* 设置TOP值为320 */

  /* 配置Timer 2以产生7.8125 kHz的PWM信号，用于转速模拟 */
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2 = 0;
  TCCR2A = _BV(COM2B1) /* 非反向PWM信号 */ | _BV(WGM20);              /* 模式5：相位校正PWM，TOP = OCR2A */
  TCCR2B = _BV(WGM22) /* 同上 */ | _BV(CS20) | _BV(CS21) | _BV(CS22); /* 预分频器 = 1024 */
  OCR2A = 255;                                                        /* 设置OCR2A为255 */

  /* 设置引脚模式 */
  pinMode(pwmInPin, INPUT); /* 输入PWM引脚 */
  /* Tachometer output signal need pullup to 5v, ref https://noctua.at/pub/media/wysiwyg/Noctua_PWM_specifications_white_paper.pdf */
  pinMode(rpmInPin, INPUT_PULLUP); /* RPM输入引脚，启用上拉电阻 */
  pinMode(pwmOutPin, OUTPUT);      /* 输出PWM引脚 */
  pinMode(pwm2OutPin, OUTPUT);     /* 第二个输出PWM引脚 */
  pinMode(hpeTachPin, OUTPUT);     /* HPE转速信号输出引脚 */
  pinMode(normalTachPin, OUTPUT);  /* 正常转速信号输出引脚 */

  /* 设置初始状态 */
  digitalWrite(hpeTachPin, LOW);                     /* HPE转速信号初始化为LOW */
  analogWrite25k(pwm2OutPin, pwm2OutDutty * pwmTop); /* 输出第二个PWM信号 */

  /*
   * 启用中断
   * attachInterrupt(digitalPinToInterrupt(rpmInPin), counter, RISING);
   * Enable PCIE2 Bit3 = 1 (Port D)
   */
  PCICR |= B00000100; /* 启用PCIE2中断 */
  /* Select PCINT20 Bit4 = 1 (Pin D4) */
  PCMSK2 |= _BV(rpmInPin); /* 选择要监测的引脚 */

  /* 禁用Nano Mini板的RGB灯 */
  CRGB leds[3] = {
      {0, 0, 0},
      {0, 0, 0},
      {0, 0, 0},
  };
  FastLED.addLeds<WS2812, 2, GRB>(leds, 3); /* 添加LED */
  FastLED.show();                           /* 更新LED状态 */

  Serial.begin(115200);           /* 初始化串口通信 */
  Serial.println("HP fan proxy"); /* 输出初始化信息 */
}

/* 读取HPE PWM信号 */
float readHpePWM(int pin, int sample) {
  uint32_t total = 0; /* 总样本计数 */
  uint32_t low = 0;   /* 低电平计数 */
  for (uint16_t i = 0; i < sample; i++) {
    low += isLow(pin); /* 统计低电平持续时间 */
    total++;           /* 增加总样本计数 */
  }
  /* 返回反转的PWM值 */
  return ((float)low / total);
}

/* 读取Intel PWM信号 */
float readIntelPWM(int pin, int sample) {
  uint32_t total = 0; /* 总样本计数 */
  uint32_t low = 0;   /* 高电平计数 */
  for (uint16_t i = 0; i < sample; i++) {
    low += isHigh(pin); /* 统计高电平持续时间 */
    total++;            /* 增加总样本计数 */
  }
  /* 返回正常的PWM值 */
  return ((float)low / total);
}

/* 在指定引脚输出PWM信号 */
void analogWrite25k(int pin, int value) {
  switch (pin) {
    case 9:
      OCR1A = value; /* 设置引脚9的PWM值 */
      break;
    case 10:
      OCR1B = value; /* 设置引脚10的PWM值 */
      break;
    default:
      /* 不支持的引脚 */
      break;
  }
}

uint32_t pulseFanPrev = 0;
uint32_t clockPrev = 0;

/* 将输入值映射到指定范围 */
float map_to_float(float x, float in_min, float in_max, float out_min, float out_max) {
  return ((x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min);
  ;
}

void loop() {
  /* 处理第一次循环 */
  if (pulseFanPrev == 0 && clockPrev == 0) {
    pulseFanPrev = pulseCount; /* 记录脉冲计数 */
    clockPrev = millis();      /* 记录时间 */
    delay(1000);               /* 等待1秒 */
    return;                    /* 返回以开始下一次循环 */
  }

  /* 计算RPM */
  uint32_t clockLoopBegin = millis();          /* 记录循环开始时间 */
  uint32_t pulse = pulseCount - pulseFanPrev;  /* 计算脉冲数 */
  uint32_t clock = clockLoopBegin - clockPrev; /* 计算时间间隔 */
  pulseFanPrev = pulseCount;                   /* 更新上一次脉冲计数 */
  clockPrev = millis();                        /* 更新上一次时间 */

  /* 计算RPM（每分钟转速） */
  uint32_t rpm = pulse * 1000 * 60 / 2 / clock; /* 根据脉冲和时间计算转速 */

  digitalWrite(hpeTachPin, rpm > 0 ? LOW : HIGH); /* 根据转速状态输出信号 */

  /* 读取反转PWM信号并输出映射值 */
  float pwmOut;
  float pwmIn = readHpePWM(pwmInPin, sample); /* 读取PWM输入信号 */
  if (pwmIn >= pwmMap[1])                     /* 根据输入PWM值选择输出 */
    pwmOut = pwmMap[3];
  else if (pwmIn <= pwmMap[0])
    pwmOut = pwmMap[2];
  else
    pwmOut = map_to_float(pwmIn, pwmMap[0], pwmMap[1], pwmMap[2], pwmMap[3]); /* 映射输入PWM */

  uint16_t out = uint16_t(pwmOut * pwmTop); /* 将输出PWM值转换为整型 */
  analogWrite25k(pwmOutPin, out);           /* 输出PWM信号 */

  /* 打印调试信息 */
  Serial.print("iLO: ");
  Serial.print(pwmIn * 100);
  Serial.print("% Out: ");
  Serial.print(pwmOut * 100);
  Serial.print("% RPM: ");
  Serial.print(rpm);
  Serial.print(" : ");
  Serial.print(pulse);
  Serial.print(" / ");
  Serial.println(clock);

  /* 固定1Hz的控制，并输出正常的转速信号 */
  clock = millis() - clockLoopBegin; /* 计算循环持续时间 */
  int rpmIn = pwmIn * rpmUpper;      /* 将输入PWM转换为RPM值 */
  if (rpmIn > timer2RpmLower) {
    int freq = timer2Freq / (rpmIn * 2 / 60); /* 根据RPM计算频率 */
    OCR2A = freq;                             /* 更新OCR2A值 */
    analogWrite(normalTachPin, freq / 2);     /* 输出正常转速信号 */
    delay(1000 - clock);                      /* 等待剩余时间 */
  } else if (rpmIn > 0) {
    int count = max(rpmIn, rpmLower) * 2 / 60; /* 计算输出脉冲的数量 */
    float half = 1000.0 / count / 2;           /* 计算每个脉冲的持续时间 */
    for (int i = 0; i < count * 2 - 1; ++i) {
      digitalWrite(normalTachPin, (i & 1) ? LOW : HIGH); /* 交替输出高低电平 */
      uint32_t now = millis() - clockLoopBegin;          /* 记录当前时间 */
      /* Serial.println(now); */
      uint32_t next = clock + half * (i + 1); /* 计算下一个脉冲的时间 */
      delay(next - now);                      /* 等待下一个脉冲 */
    }
    digitalWrite(normalTachPin, LOW);  /* 结束脉冲输出 */
    clock = millis() - clockLoopBegin; /* 更新循环时间 */
    if (clock < 1000) {
      delay(1000 - clock); /* 等待直到1秒结束 */
    }
  } else {
    delay(1000 - clock); /* 如果RPM为0，等待剩余时间 */
  }
}