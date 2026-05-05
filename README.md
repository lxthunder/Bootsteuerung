# Bootsteuerung

Arduino-Sketch zur Steuerung eines RC-Bootes mit Joystick, BTS7960 H-Brücken-Motortreiber und Lenkservo.

## Hardware

| Komponente | Beschreibung |
|------------|-------------|
| Arduino Nano / Uno | Mikrocontroller |
| BTS7960 43A H-Brücke | Motortreiber (IBT-2-Modul) |
| Joystick-Modul | X/Y-Achse + Taster |
| Servo | Lenkung |
| Motor | Bootsmotor (max. 43 A, 6–27 V) |

## Pinbelegung

### BTS7960 Motortreiber

| Arduino-Pin | BTS7960-Pin | Funktion |
|-------------|-------------|----------|
| 5 (PWM) | RPWM | Vorwärts-PWM |
| 6 (PWM) | LPWM | Rückwärts-PWM |
| 7 | R_EN | Vorwärts-Enable |
| 8 | L_EN | Rückwärts-Enable |
| 5 V | VCC | Logikversorgung |
| GND | GND | Masse |

Motorversorgung (6–27 V) an **B+** / **B−**, Motor an **M+** / **M−**.

### Sonstige Pins

| Arduino-Pin | Komponente | Funktion |
|-------------|------------|----------|
| A0 | Joystick X | Lenkung |
| A1 | Joystick Y | Gas / Rückwärts |
| 2 | Joystick SW | Hold-Taster |
| 10 (PWM) | Servo Signal | Lenkservo |

## Funktionen

### Steuerung
- **Joystick X (A0)** → Lenkservo 0–180°
- **Joystick Y (A1)** → Motor vorwärts (+255) / Stopp (0) / rückwärts (−255)
- Beide Achsen mit **x³-Kennlinie**: träge Reaktion in der Mittelstellung, progressiv zu den Anschlägen

### Hold-Funktion
- Joystick-Taster einmal drücken → **Lenkwinkel und Motorgeschwindigkeit einfrieren**
- Nochmal drücken → **freigeben**
- LED-Rückmeldung über Serial Monitor (`Hold=1`)

## Sicherheit

### Kurzschluss-Schutz (Software)

Der BTS7960 wird durch **Shoot-Through** zerstört, wenn RPWM und LPWM gleichzeitig HIGH sind (verbindet +VM direkt mit GND).

Die Software verhindert dies durch:

1. **Strikte Schreibreihenfolge**: Bei jedem Aufruf von `setMotor()` wird zuerst die *Gegenrichtung* abgeschaltet, dann die neue Richtung eingeschaltet.

   ```
   Vorwärts:  LPWM = 0  →  RPWM = speed
   Rückwärts: RPWM = 0  →  LPWM = speed
   ```

2. **Totzeit bei Richtungswechsel**: Wechsel von Vorwärts auf Rückwärts (oder umgekehrt) löst eine 30 ms Pause aus, in der beide PWM-Ausgänge auf 0 gesetzt sind. Verhindert Gegenstrom-Spikes durch Motor-Induktivität.

3. **Sichere Startup-Sequenz**: RPWM und LPWM werden explizit auf LOW gesetzt, **bevor** R_EN und L_EN auf HIGH geschaltet werden. Verhindert undefinierten Zustand beim Einschalten.

### Hardware-Schutz (BTS7960)
- Interne Übertemperaturabschaltung
- Überstromschutz (IS-Pins, optional auswertbar)
- Interne Pull-Downs auf IN-Pins: floating = LOW = sicher

## Serial Monitor

Baudrate: **9600**

Ausgabe pro Loop-Iteration (alle 20 ms):

```
X=512 Y=510 Srv=90 Speed=0 Hold=0
X=512 Y=800 Srv=90 Speed=187 Hold=0
X=400 Y=800 Srv=62 Speed=187 Hold=1
```

| Feld | Bedeutung |
|------|-----------|
| X | Joystick X-Rohwert (0–1023) |
| Y | Joystick Y-Rohwert (0–1023) |
| Srv | Aktueller Lenkwinkel (0–180°) |
| Speed | Motorgeschwindigkeit (−255…+255) |
| Hold | Hold-Status (0 = aus, 1 = eingefroren) |

## Abhängigkeiten

- `Servo.h` (in der Arduino IDE enthalten)
