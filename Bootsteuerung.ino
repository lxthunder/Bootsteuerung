#include <Servo.h>
#include <EEPROM.h>

// ── BTS7960 H-Brücke ────────────────────────────────────────────────────────
#define RPWM_PIN     5   // Vorwärts-PWM  (Arduino PWM-Pin)
#define LPWM_PIN     6   // Rückwärts-PWM (Arduino PWM-Pin)
#define R_EN_PIN     7   // Vorwärts-Enable (HIGH = aktiv)
#define L_EN_PIN     8   // Rückwärts-Enable (HIGH = aktiv)

// ── Sonstige Pins ───────────────────────────────────────────────────────────
#define SERVO_PIN    10
#define JOY_X_PIN    A1
#define JOY_Y_PIN    A0
#define BTN_HOLD_PIN  2  // Joystick-Taster (SW), LOW = gedrückt

// ── Joystick-Kalibrierung (EEPROM, geschrieben durch Joystick_Test) ─────────
const int      EEPROM_ADDR  = 0;
const uint16_t EEPROM_MAGIC = 0xCA01;   // muss zu Joystick_Test passen
const int      JOY_DEADZONE   = 30;     // ADC-Counts um Mitte -> 0
const uint8_t  JOY_OVERSAMPLE = 8;      // ADC-Samples pro Loop (gemittelt, kein Lag)
const float    JOY_SMOOTHING  = 0.07f;  // EMA-Faktor 0..1: klein = ruhiger/traeger

// Knick-Kennlinie (zweistueckig linear): erste KNEE_IN des Sticks deckt
// nur KNEE_OUT der Stellgroesse ab, der Rest geht steiler bis Endausschlag.
// 75% Stick -> 50% Stellgroesse (Servo: 45° Auslenkung von 90°),
// letztes Viertel -> 45°..90° Auslenkung.
const float    KNEE_IN  = 0.75f;
const float    KNEE_OUT = 0.5f;

struct Kalibrierung {
  uint16_t magic;
  int16_t  xCenter, xMin, xMax;
  int16_t  yCenter, yMin, yMax;
};

Kalibrierung kal;

Servo steeringServo;

int  heldMotorSpeed  = 0;
int  heldServoAngle  = 90;
bool holdActive      = false;
bool prevBtnHold     = false;

// Knick-Kennlinie auf normiertem Eingang n in [0..1]:
// Abschnitt 1 (n <= KNEE_IN):  flach bis KNEE_OUT
// Abschnitt 2 (n >  KNEE_IN):  steiler bis 1.0
float kneeCurve(float n) {
  if (n <= KNEE_IN) return n * (KNEE_OUT / KNEE_IN);
  return KNEE_OUT + (n - KNEE_IN) * (1.0f - KNEE_OUT) / (1.0f - KNEE_IN);
}

// Servo-Mapping: Kalibrierung + Deadzone + Knick-Kennlinie.
int applyKneeKal(int raw, int center, int minV, int maxV,
                 int outMin, int outMax) {
  int outMid = (outMin + outMax) / 2;
  int delta  = raw - center;
  if (delta > -JOY_DEADZONE && delta < JOY_DEADZONE) return outMid;

  if (delta > 0) {
    int span = max(1, maxV - center - JOY_DEADZONE);
    float n = (float)(delta - JOY_DEADZONE) / span;
    if (n > 1.0f) n = 1.0f;
    return constrain(outMid + (int)(kneeCurve(n) * (outMax - outMid)), outMin, outMax);
  } else {
    int span = max(1, center - minV - JOY_DEADZONE);
    float n = (float)(-delta - JOY_DEADZONE) / span;
    if (n > 1.0f) n = 1.0f;
    return constrain(outMid - (int)(kneeCurve(n) * (outMid - outMin)), outMin, outMax);
  }
}

// Motor-Mapping: Kalibrierung + Deadzone, linear ueber den ganzen Bereich.
int applyLinearKal(int raw, int center, int minV, int maxV,
                   int outMin, int outMax) {
  int outMid = (outMin + outMax) / 2;
  int delta  = raw - center;
  if (delta > -JOY_DEADZONE && delta < JOY_DEADZONE) return outMid;

  if (delta > 0) {
    long out = (long)(delta - JOY_DEADZONE) * (outMax - outMid)
               / max(1, (maxV - center - JOY_DEADZONE));
    return constrain(outMid + (int)out, outMin, outMax);
  } else {
    long out = (long)(delta + JOY_DEADZONE) * (outMid - outMin)
               / max(1, (center - minV - JOY_DEADZONE));
    return constrain(outMid + (int)out, outMin, outMax);
  }
}

bool ladeKalibrierung() {
  EEPROM.get(EEPROM_ADDR, kal);
  return kal.magic == EEPROM_MAGIC;
}

void setzeKalibrierungDefault() {
  kal.xCenter = 512; kal.xMin = 0; kal.xMax = 1023;
  kal.yCenter = 512; kal.yMin = 0; kal.yMax = 1023;
}

// speed: -255 = voll rückwärts, 0 = Stopp, +255 = voll vorwärts
void setMotor(int speed) {
  speed = constrain(speed, -255, 255);

  static int8_t prevDir = 0;
  int8_t dir = (speed > 0) ? 1 : (speed < 0 ? -1 : 0);

  // Richtungswechsel: kurze Totzeit verhindert Gegenstromspike
  if (dir != 0 && prevDir != 0 && dir != prevDir) {
    analogWrite(RPWM_PIN, 0);
    analogWrite(LPWM_PIN, 0);
    delay(30);
  }
  prevDir = dir;

  if (speed > 0) {
    analogWrite(LPWM_PIN, 0);      // erst Gegenrichtung AUS
    analogWrite(RPWM_PIN, speed);  // dann Vorwärts EIN
  } else if (speed < 0) {
    analogWrite(RPWM_PIN, 0);      // erst Gegenrichtung AUS
    analogWrite(LPWM_PIN, -speed); // dann Rückwärts EIN
  } else {
    analogWrite(RPWM_PIN, 0);
    analogWrite(LPWM_PIN, 0);
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(BTN_HOLD_PIN, INPUT_PULLUP);

  // PWM-Pins explizit LOW setzen, BEVOR EN-Pins aktiviert werden
  pinMode(RPWM_PIN, OUTPUT);
  pinMode(LPWM_PIN, OUTPUT);
  digitalWrite(RPWM_PIN, LOW);
  digitalWrite(LPWM_PIN, LOW);

  pinMode(R_EN_PIN, OUTPUT);
  pinMode(L_EN_PIN, OUTPUT);
  digitalWrite(R_EN_PIN, HIGH);
  digitalWrite(L_EN_PIN, HIGH);

  steeringServo.attach(SERVO_PIN, 500, 2500);
  steeringServo.writeMicroseconds(1500);

  delay(500);

  if (ladeKalibrierung()) {
    Serial.println(F(">> EEPROM: Joystick-Kalibrierung GEFUNDEN"));
    Serial.print(F("   Mitte X=")); Serial.print(kal.xCenter);
    Serial.print(F(" Y="));         Serial.println(kal.yCenter);
    Serial.print(F("   X-Bereich ")); Serial.print(kal.xMin);
    Serial.print(F(".."));            Serial.println(kal.xMax);
    Serial.print(F("   Y-Bereich ")); Serial.print(kal.yMin);
    Serial.print(F(".."));            Serial.println(kal.yMax);
  } else {
    Serial.println(F(">> EEPROM: KEINE gueltige Kalibrierung -> Default 0..1023"));
    Serial.println(F("   Hinweis: Joystick_Test (Hardwaretesting) zum Kalibrieren ausfuehren"));
    setzeKalibrierungDefault();
  }

  Serial.println(F("Bootsteuerung bereit (BTS7960)"));
}

int leseAchse(uint8_t pin) {
  uint16_t sum = 0;
  for (uint8_t i = 0; i < JOY_OVERSAMPLE; i++) sum += analogRead(pin);
  return sum / JOY_OVERSAMPLE;
}

void loop() {
  static float joyXfilt = 512.0f;
  static float joyYfilt = 512.0f;

  joyXfilt += JOY_SMOOTHING * (leseAchse(JOY_X_PIN) - joyXfilt);
  joyYfilt += JOY_SMOOTHING * (leseAchse(JOY_Y_PIN) - joyYfilt);

  int  joyX    = (int)joyXfilt;
  int  joyY    = (int)joyYfilt;
  bool btnHold = !digitalRead(BTN_HOLD_PIN);

  int servoAngle = applyKneeKal  (joyX, kal.xCenter, kal.xMin, kal.xMax,    0,  180);
  int motorSpeed = applyLinearKal(joyY, kal.yCenter, kal.yMin, kal.yMax, -255,  255);

  // Hold-Toggle: Taster einmal → einfrieren, nochmal → freigeben
  if (btnHold && !prevBtnHold) {
    if (!holdActive) {
      heldServoAngle = servoAngle;
      heldMotorSpeed = motorSpeed;
      holdActive     = true;
    } else {
      holdActive = false;
    }
  }
  prevBtnHold = btnHold;

  if (holdActive) {
    steeringServo.write(heldServoAngle);
    setMotor(heldMotorSpeed);
  } else {
    steeringServo.write(servoAngle);
    setMotor(motorSpeed);
  }

  Serial.print("X=");      Serial.print(joyX);
  Serial.print(" Y=");     Serial.print(joyY);
  Serial.print(" Srv=");   Serial.print(holdActive ? heldServoAngle : servoAngle);
  Serial.print(" Mot=");   Serial.println(holdActive ? heldMotorSpeed : motorSpeed);

  delay(20);
}
