#include <FastLED.h>

#define PWM_IN_PIN         A1
#define PWM_OUT_PIN        9 
#define PWM2_OUT_PIN       10
#define RPM_IN_PIN         4
#define HPE_TACH_PIN       A2  
#define NORMAL_TACH_PIN    3

#define PWM_TOP            320
#define PWM_SAMPLE         25000
#define PWM_MAP            {10, 20, 50, 100}  // In percentage
#define RPM_UPPER          6000
#define RPM_LOWER          60

#define TIMER2_FREQ        (16000000 / 1024 / 2)
#define TIMER2_RPM_LOWER   900 
#define PWM2_OUT_DUTY      45  // In percentage

volatile uint32_t pulseCount = 0;

void counter() {
  pulseCount++;  
}

float readPWM(uint8_t pin, uint16_t sample, bool invert=false) {
  uint32_t total = 0, high = 0;
  
  for (uint16_t i = 0; i < sample; i++) {    
    high += digitalRead(pin);
    total++;
  }
  
  return invert ? (float)(total - high) / total : (float)high / total;
}

void setupTimer1() {  
  TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM11); 
  TCCR1B = _BV(WGM13) | _BV(CS10);
  ICR1 = PWM_TOP;
}

void setupTimer2() {
  TCCR2A = _BV(COM2B1) | _BV(WGM20);
  TCCR2B = _BV(WGM22) | _BV(CS20) | _BV(CS21) | _BV(CS22);   
  OCR2A  = 255;
}

void analogWrite25k(uint8_t pin, uint16_t val) {
  if (pin == 9)         OCR1A = val;
  else if (pin == 10)   OCR1B = val;  
}

void setupRpmPin() {
  pinMode(RPM_IN_PIN, INPUT_PULLUP);
  PCICR  |= _BV(PCIE2);
  PCMSK2 |= _BV(PCINT20);
}

ISR (PCINT2_vect) {
  if (digitalRead(RPM_IN_PIN) == HIGH) {
    pulseCount++;
  }  
}

void disableRgbLed() {
  CRGB leds[3] = { {0,0,0}, {0,0,0}, {0,0,0} };
  FastLED.addLeds<WS2812, 2, GRB>(leds, 3);
  FastLED.show();
}

void setup() {
  setupTimer1();
  setupTimer2();
  setupRpmPin();
  disableRgbLed();
   
  pinMode(PWM_IN_PIN, INPUT);
  pinMode(PWM_OUT_PIN, OUTPUT);
  pinMode(PWM2_OUT_PIN, OUTPUT);
  pinMode(HPE_TACH_PIN, OUTPUT);
  pinMode(NORMAL_TACH_PIN, OUTPUT);
  
  digitalWrite(HPE_TACH_PIN, LOW);
  
  analogWrite25k(PWM2_OUT_PIN, PWM2_OUT_DUTY * PWM_TOP / 100);
}

void loop() {
  static uint32_t prevPulse = 0, prevTime = 0; 
  
  if (prevPulse == 0) {
    prevPulse = pulseCount;
    prevTime = micros();
    delay(1000);
    return;
  }
  
  uint32_t curTime = micros();
  uint32_t deltaTime = curTime - prevTime;
  uint32_t deltaPulse = pulseCount - prevPulse;
  prevPulse = pulseCount;
  prevTime = curTime;    
  
  noInterrupts();
  uint32_t rpm = deltaPulse * 60000000 / 2 / deltaTime;
  interrupts();
  
  rpm = constrain(rpm, RPM_LOWER, RPM_UPPER);
  
  digitalWrite(HPE_TACH_PIN, rpm > 0 ? LOW : HIGH);
  
  float pwmIn = readPWM(PWM_IN_PIN, PWM_SAMPLE, true);
  float pwmOut = constrain(pwmIn * 100, PWM_MAP[0], PWM_MAP[3]);
  pwmOut = map(pwmOut, PWM_MAP[0], PWM_MAP[1], PWM_MAP[2], PWM_MAP[3]);  
  analogWrite25k(PWM_OUT_PIN, pwmOut * PWM_TOP / 100);
    
  if (rpm >= TIMER2_RPM_LOWER) {
    OCR2A = TIMER2_FREQ / (rpm * 2 / 60);
    analogWrite(NORMAL_TACH_PIN, OCR2A/2); 
  }
  else if (rpm > 0) {    
    uint16_t count = max(rpm, RPM_LOWER) * 2 / 60;
    float halfPeriod = 500000 / count;
    
    for (uint16_t i = 0; i < count*2; i++) {
      digitalWrite(NORMAL_TACH_PIN, (i % 2) ? LOW : HIGH);      
      delayMicroseconds(halfPeriod);
    }
  }
  
  while (micros() - curTime < 1000000); // Align to 1 second interval
}
