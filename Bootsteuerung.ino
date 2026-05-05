#include <Servo.h>

// ── BTS7960 H-Brücke ────────────────────────────────────────────────────────
#define RPWM_PIN     5   // Vorwärts-PWM  (Arduino PWM-Pin)
#define LPWM_PIN     6   // Rückwärts-PWM (Arduino PWM-Pin)
#define R_EN_PIN     7   // Vorwärts-Enable (HIGH = aktiv)
#define L_EN_PIN     8   // Rückwärts-Enable (HIGH = aktiv)

// ── Sonstige Pins ───────────────────────────────────────────────────────────
#define SERVO_PIN    10
#define JOY_X_PIN    A0
#define JOY_Y_PIN    A1
#define BTN_HOLD_PIN  2  // Joystick-Taster (SW), LOW = gedrückt

Servo steeringServo;

int  heldMotorSpeed  = 0;
int  heldServoAngle  = 90;
bool holdActive      = false;
bool prevBtnHold     = false;

// x³-Kurve: träge in der Mitte, progressiv zu den Enden
int applyCurve(int raw, int inMin, int inMax, int outMin, int outMax) {
  float n = (raw - (inMin + inMax) / 2.0f) / ((inMax - inMin) / 2.0f);
  n = constrain(n, -1.0f, 1.0f);
  float curved = n * n * n;
  int outMid   = (outMin + outMax) / 2;
  int outRange = (outMax - outMin) / 2;
  return outMid + (int)(curved * outRange);
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

  steeringServo.attach(SERVO_PIN, 1000, 2000);
  steeringServo.writeMicroseconds(1500);

  delay(500);

  Serial.println("Bootsteuerung bereit (BTS7960)");
}

void loop() {
  int  joyX    = analogRead(JOY_X_PIN);
  int  joyY    = analogRead(JOY_Y_PIN);
  bool btnHold = !digitalRead(BTN_HOLD_PIN);

  int servoAngle = applyCurve(joyX, 0, 1023,  0,    180);
  int motorSpeed = applyCurve(joyY, 0, 1023, -255,  255);

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
  Serial.print(" Speed="); Serial.print(holdActive ? heldMotorSpeed : motorSpeed);
  Serial.print(" Hold=");  Serial.println(holdActive);

  delay(20);
}
