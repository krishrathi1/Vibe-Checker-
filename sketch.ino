/*
 ============================================================
   DIGITAL CROWD MOOD ESTIMATOR — v3.0
   Fix: Energy-based detection (variance fails with pot)
   New: Energy %, mood timer, peak marker, siren non-blocking
 ============================================================
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Pins
const int SOUND_PIN   = A0;
const int LED_CALM    = 8;
const int LED_ACTIVE  = 9;
const int LED_EXCITED = 10;
const int LED_CHAOTIC = 11;
const int BUZZER_PIN  = 12;

const int SAMPLE_WINDOW = 40;
float noiseFloor  = 0;
float peakEnergy  = 0;
int   lastMoodLvl = 0;
unsigned long moodStartMs = 0;

// Non-blocking siren state
static int  sirenFreq = 500;
static bool sirenUp   = true;
static unsigned long lastSirenMs = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Vertical bar chars (used as fill blocks in level meter)
byte bar1[8] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F};
byte bar2[8] = {0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x1F};
byte bar3[8] = {0x00,0x00,0x00,0x00,0x00,0x1F,0x1F,0x1F};
byte bar4[8] = {0x00,0x00,0x00,0x00,0x1F,0x1F,0x1F,0x1F};
byte bar5[8] = {0x00,0x00,0x00,0x1F,0x1F,0x1F,0x1F,0x1F};
byte bar6[8] = {0x00,0x00,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F};
byte bar7[8] = {0x00,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F};
byte bar8[8] = {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F};

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();

  lcd.createChar(0, bar1); lcd.createChar(1, bar2);
  lcd.createChar(2, bar3); lcd.createChar(3, bar4);
  lcd.createChar(4, bar5); lcd.createChar(5, bar6);
  lcd.createChar(6, bar7); lcd.createChar(7, bar8);

  pinMode(LED_CALM,    OUTPUT);
  pinMode(LED_ACTIVE,  OUTPUT);
  pinMode(LED_EXCITED, OUTPUT);
  pinMode(LED_CHAOTIC, OUTPUT);
  pinMode(BUZZER_PIN,  OUTPUT);

  // Splash screen
  lcd.setCursor(0,0); lcd.print(" CROWD ANALYZER ");
  lcd.setCursor(0,1); lcd.print("   v3.0 READY   ");
  delay(1500);

  // Auto-calibration: captures baseline (noise floor)
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("CALIBRATING...");
  lcd.setCursor(0,1);
  long calibSum = 0;
  for (int i = 0; i < 16; i++) {
    calibSum += analogRead(SOUND_PIN);
    lcd.write((uint8_t)7); // full block progress bar
    delay(100);
  }
  noiseFloor = (float)calibSum / 16.0f;

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("BASE: ");
  lcd.print((int)noiseFloor);
  lcd.setCursor(0,1); lcd.print("  SYSTEM READY! ");
  delay(900);
  lcd.clear();

  moodStartMs = millis();
}

void loop() {
  float samples[SAMPLE_WINDOW];
  float sum = 0;

  // High-speed sampling
  for (int i = 0; i < SAMPLE_WINDOW; i++) {
    samples[i] = abs((float)analogRead(SOUND_PIN) - noiseFloor);
    sum += samples[i];
    delay(2);
  }
  float avgEnergy = sum / SAMPLE_WINDOW;

  // Peak hold with slow decay
  if (avgEnergy > peakEnergy) peakEnergy = avgEnergy;
  else                         peakEnergy *= 0.995f;

  // Energy % (0-100), capped at ADC max range from floor
  int pct = (int)constrain(avgEnergy * 100.0f / 1023.0f, 0, 100);

  // --- MOOD LOGIC (FIXED: energy-based, not variance-based) ---
  // Variance was always ~0 with a potentiometer (steady DC signal).
  // avgEnergy = deviation from calibrated floor, tracks pot position.
  const char* mood;
  int moodLvl;
  if      (avgEnergy < 150)  { mood = "CALM   "; moodLvl = 1; }
  else if (avgEnergy < 400)  { mood = "ACTIVE "; moodLvl = 2; }
  else if (avgEnergy < 700)  { mood = "EXCITED"; moodLvl = 3; }
  else                       { mood = "CHAOTIC"; moodLvl = 4; }

  // Mood change: reset duration timer
  if (moodLvl != lastMoodLvl) {
    lastMoodLvl = moodLvl;
    moodStartMs = millis();
    sirenFreq = 500; sirenUp = true; // reset siren on mode change
  }
  int durSecs = (int)constrain((millis() - moodStartMs) / 1000UL, 0, 999);

  updateLEDs(moodLvl);

  // LCD Row 0: "EXCITED 72%  5s"  (7 + 3 + 1 + 1 + 3 + 1 = 16 chars)
  lcd.setCursor(0, 0);
  char row0[17];
  snprintf(row0, 17, "%s%3d%% %3ds", mood, pct, durSecs);
  lcd.print(row0);

  // LCD Row 1: 15-cell level meter + 1-cell peak marker
  lcd.setCursor(0, 1);
  int meterPos = (int)map((long)constrain(avgEnergy, 0, 1023), 0, 1023, 0, 15);
  int peakPos  = (int)map((long)constrain(peakEnergy, 0, 1023), 0, 1023, 0, 15);
  for (int i = 0; i < 16; i++) {
    if      (i < meterPos)                    lcd.write((uint8_t)7); // full block
    else if (i == peakPos && peakPos > meterPos) lcd.print('|');     // peak marker
    else                                          lcd.print(' ');
  }

  updateBuzzer(moodLvl);

  // Serial data log
  Serial.print("E:"); Serial.print(avgEnergy, 1);
  Serial.print(" Pk:"); Serial.print(peakEnergy, 1);
  Serial.print(" %:"); Serial.print(pct);
  Serial.print(" Mood:"); Serial.print(mood);
  Serial.print(" Dur:"); Serial.print(durSecs);
  Serial.println("s");

  delay(50);
}

void updateLEDs(int level) {
  digitalWrite(LED_CALM,    level == 1 ? HIGH : LOW);
  digitalWrite(LED_ACTIVE,  level == 2 ? HIGH : LOW);
  digitalWrite(LED_EXCITED, level == 3 ? HIGH : LOW);
  digitalWrite(LED_CHAOTIC, level == 4 ? HIGH : LOW);
}

void updateBuzzer(int level) {
  unsigned long now = millis();
  if (level == 4) {
    // Non-blocking siren sweep: updates once per call (~130ms cycle)
    tone(BUZZER_PIN, sirenFreq);
    sirenFreq += sirenUp ? 25 : -25;
    if (sirenFreq >= 1100) sirenUp = false;
    if (sirenFreq <= 450)  sirenUp = true;
  } else if (level == 3) {
    // Intermittent double-beep for EXCITED
    if (now - lastSirenMs >= 600) {
      lastSirenMs = now;
      tone(BUZZER_PIN, 900, 60);
    }
  } else {
    noTone(BUZZER_PIN);
    sirenFreq = 500;
    sirenUp   = true;
  }
}
