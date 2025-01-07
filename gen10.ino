// HPE Gen10 fan proxy for Noctua NF-A12 PWM fan
#include <FastLED.h>
#define pinOfPin(P)\
  (((P)>=0&&(P)<8)?&PIND:(((P)>7&&(P)<14)?&PINB:&PINC))
#define pinIndex(P)((uint8_t)(P>13?P-14:P&7))
#define pinMask(P)((uint8_t)(1<<pinIndex(P)))
#define isHigh(P)((*(pinOfPin(P))& pinMask(P))>0)
#define isLow(P)((*(pinOfPin(P))& pinMask(P))==0)

const int pwmInPin = A1;
const int pwmOutPin = 9;
const int pwm2OutPin = 10;
const int rpmInPin = 4;
const int hpeTachPin = A2;
const int normalTachPin = 3;
const uint16_t pwmTop = 320;
const int sample = 25000;
const float pwmMap[4] = {0.35, 0.45, 0.20, 0.65};
const int rpmUpper = 3400;
const int rpmLower = 60;
const float timer2Freq = 16000000 / 1024 / 2;
const int timer2RpmLower = 900;
const float pwm2OutDutty = 0.45;

volatile uint32_t pulseCount = 0;

void counter()
{
	pulseCount++;
}

ISR (PCINT2_vect)
{
  if (digitalRead(rpmInPin) == HIGH)
	  pulseCount++;
}

void setup()
{
  // Configure Timer 1 for PWM @ 25 kHz.
  TCCR1A = 0;           // undo the configuration done by...
  TCCR1B = 0;           // ...the Arduino core library
  TCNT1  = 0;           // reset timer
  TCCR1A = _BV(COM1A1)  // non-inverted PWM on ch. A
         | _BV(COM1B1)  // same on ch; B
         | _BV(WGM11);  // mode 10: ph. correct PWM, TOP = ICR1
  TCCR1B = _BV(WGM13)   // ditto
         | _BV(CS10);   // prescaler = 1
  ICR1   = pwmTop;      // TOP = 320

  // Configure Timer 2 for PWM @ 7.8125 kHz. for tach simulate
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2  = 0;
  TCCR2A = _BV(COM2B1)  // non-inverted PWM on ch. B
      | _BV(WGM20);  // mode 5: ph. correct PWM, TOP = OCR2A
  TCCR2B = _BV(WGM22)   // ditto
      | _BV(CS20)| _BV(CS21)| _BV(CS22);   // prescaler = 1024
  OCR2A  = 255;

  pinMode(pwmInPin, INPUT);
  // Tachometer output signal need pullup to 5v, ref https://noctua.at/pub/media/wysiwyg/Noctua_PWM_specifications_white_paper.pdf
  pinMode(rpmInPin, INPUT_PULLUP);
  pinMode(pwmOutPin, OUTPUT);
  pinMode(pwm2OutPin, OUTPUT);
  pinMode(hpeTachPin, OUTPUT);
  pinMode(normalTachPin, OUTPUT);
  // Tach low
  digitalWrite(hpeTachPin, LOW);
  // pwm2 
  analogWrite25k(pwm2OutPin, pwm2OutDutty * pwmTop);
  // RPM In
  //attachInterrupt(digitalPinToInterrupt(rpmInPin), counter, RISING);
  // Enable PCIE2 Bit3 = 1 (Port D)
  PCICR |= B00000100;
  // Select PCINT20 Bit4 = 1 (Pin D4)
  PCMSK2 |= _BV(rpmInPin);

  // Disable ugly rgb of nano mini board
  CRGB leds[3] = {
    {0, 0, 0},
    {0, 0, 0},
    {0, 0, 0},
  };
  FastLED.addLeds<WS2812, 2, GRB>(leds, 3);
  FastLED.show();

  Serial.begin(115200);
  Serial.println("HP fan proxy");
}

float readHpePWM(int pin, int sample) {
  uint32_t total = 0;
  uint32_t low = 0;
  for (uint16_t i = 0; i < sample; i++) {
    low += isLow(pin) ;
    total ++;
  }
  // Inversed PWM
  return (float) low / total;
}

float readIntelPWM(int pin, int sample) {
  uint32_t total = 0;
  uint32_t low = 0;
  for (uint16_t i = 0; i < sample; i++) {
    low += isHigh(pin) ;
    total ++;
  }
  // Normal PWM
  return (float) low / total;
}

void analogWrite25k(int pin, int value)
{
  switch (pin) {
    case 9:
      OCR1A = value;
      break;
    case 10:
      OCR1B = value;
      break;
    default:
      // no other pin will work
      break;
  }
}
uint32_t pulseFanPrev = 0; 
uint32_t clockPrev = 0;

float map_to_float(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;;
}

void loop()
{
  // handle first loop
  if (pulseFanPrev == 0 && clockPrev == 0) {
    pulseFanPrev = pulseCount;
    clockPrev = millis();
    delay(1000);
    return;
  }
  // compute rpm
  uint32_t clockLoopBegin = millis();
  uint32_t pulse = pulseCount - pulseFanPrev;
  uint32_t clock = clockLoopBegin - clockPrev;
  pulseFanPrev = pulseCount;
  clockPrev = millis();
  uint32_t rpm = pulse * 1000 * 60 / 2 / clock; /* 计算转速 */

  digitalWrite(hpeTachPin, rpm > 0 ? LOW: HIGH);

  // read inversed pwm(about 22ms with 25000 samples) and output mapped, 
  float pwmOut;
  float pwmIn = readHpePWM(pwmInPin, sample);
  if (pwmIn >= pwmMap[1])
    pwmOut = pwmMap[3];
  else if (pwmIn <= pwmMap[0])
    pwmOut = pwmMap[2];
  else
    pwmOut = map_to_float(pwmIn, pwmMap[0], pwmMap[1], pwmMap[2], pwmMap[3]);
  uint16_t out = uint16_t(pwmOut * pwmTop);
  analogWrite25k(pwmOutPin, out);

  // debug out
  Serial.print("iLO: "); Serial.print(pwmIn * 100); 
  Serial.print("% Out: "); Serial.print(pwmOut * 100); 
  Serial.print("% RPM: "); Serial.print(rpm); Serial.print(" : "); Serial.print(pulse); Serial.print(" / "); Serial.println(clock);


  if (rpm > 0) {
    /* 每秒输出2*RPM个脉冲 */
    float pulseInterval = 1000000.0 / (rpm * 2); /* 计算脉冲间隔(微秒) */
    digitalWrite(hpeTachPin, LOW);
    delayMicroseconds(pulseInterval / 2);
    digitalWrite(hpeTachPin, HIGH);
    delayMicroseconds(pulseInterval / 2);
  } else {
    digitalWrite(hpeTachPin, HIGH); /* 无转速时保持 HIGH */
  }
}