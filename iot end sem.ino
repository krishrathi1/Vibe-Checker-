/*
 ============================================================
   DIGITAL CROWD MOOD ESTIMATOR — ADVANCED v2.0
   Features: Auto-Calibration, Signal Metering, Siren Alerts
   Auto-Detect LCD Address (0x27 or 0x3F)
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

// Settings
const int SAMPLE_WINDOW = 40; 
float noiseFloor = 0;         
float peakScore = 0;       
const unsigned long SERIAL_TIMEOUT_MS = 3000;

// Serial AI override state
bool serialOverride = false;
unsigned long lastSerialMsgMs = 0;
String serialMood = "CALM   ";
int serialMoodLvl = 1;
int serialScore = 0; // 0..100
String serialLine = "";

// Use fixed Wokwi LCD I2C address for stability
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Custom Characters for Pulse Meter
byte bar1[8] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1F};
byte bar2[8] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1F, 0x1F};
byte bar3[8] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x1F, 0x1F, 0x1F};
byte bar4[8] = {0x0, 0x0, 0x0, 0x0, 0x1F, 0x1F, 0x1F, 0x1F};
byte bar5[8] = {0x0, 0x0, 0x0, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
byte bar6[8] = {0x0, 0x0, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
byte bar7[8] = {0x0, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
byte bar8[8] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};

void setup() {
  Serial.begin(115200);

  lcd.init();
  lcd.backlight();
  
  // Create Custom Meter Characters
  lcd.createChar(0, bar1); lcd.createChar(1, bar2);
  lcd.createChar(2, bar3); lcd.createChar(3, bar4);
  lcd.createChar(4, bar5); lcd.createChar(5, bar6);
  lcd.createChar(6, bar7); lcd.createChar(7, bar8);

  pinMode(LED_CALM, OUTPUT); pinMode(LED_ACTIVE, OUTPUT);
  pinMode(LED_EXCITED, OUTPUT); pinMode(LED_CHAOTIC, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Startup sanity blink so we can confirm firmware is executing.
  digitalWrite(LED_CALM, HIGH); delay(150);
  digitalWrite(LED_CALM, LOW);  delay(150);

  // Splash Screen
  lcd.setCursor(1, 0); lcd.print("CROWD ANALYZER");
  lcd.setCursor(2, 1); lcd.print("v2.0 BOOTING");
  delay(1500);

  // STEP: Auto-Calibration
  lcd.clear();
  lcd.print("CALIBRATING...");
  lcd.setCursor(0, 1);
  long calibSum = 0;
  for(int i=0; i<16; i++) {
    calibSum += analogRead(SOUND_PIN);
    lcd.print(">");
    delay(100);
  }
  noiseFloor = (float)calibSum / 16.0;
  lcd.clear();
}

void loop() {
  handleSerialInput();

  // If external AI is sending mood updates, use them as the source of truth.
  if (serialOverride && (millis() - lastSerialMsgMs) <= SERIAL_TIMEOUT_MS) {
    updateHardware(serialMoodLvl);
    renderMood(serialMood, serialScore);
    delay(50);
    return;
  }

  serialOverride = false;

  float samples[SAMPLE_WINDOW];
  float sum = 0;

  // 1. Capture High-Speed Samples
  for (int i = 0; i < SAMPLE_WINDOW; i++) {
    samples[i] = abs(analogRead(SOUND_PIN) - noiseFloor);
    sum += samples[i];
    delay(2);
  }
  float avgEnergy = sum / SAMPLE_WINDOW;

  // 2. Calculate Variance (Variability)
  float varianceSum = 0;
  for (int i = 0; i < SAMPLE_WINDOW; i++) {
    varianceSum += pow(samples[i] - avgEnergy, 2);
  }
  float variance = varianceSum / SAMPLE_WINDOW;

  // 3. Build a score that responds to both level and fluctuation.
  float crowdScore = (avgEnergy * 0.85) + (sqrt(variance) * 0.15);

  // Adaptive normalization for microphone input so mood transitions
  // remain visible across different ambient environments.
  static float adaptiveMax = 80.0;
  if (crowdScore > adaptiveMax) adaptiveMax = crowdScore;
  adaptiveMax = max(40.0, adaptiveMax * 0.995);
  float normalizedScore = constrain((crowdScore / adaptiveMax) * 100.0, 0.0, 100.0);

  // 4. Peak Tracking
  if (crowdScore > peakScore) peakScore = crowdScore;

  // 5. Mood Logic (adaptive for microphone input)
  String mood;
  int moodLvl = 0;
  if      (normalizedScore < 25) { mood = "CALM   "; moodLvl = 1; }
  else if (normalizedScore < 50) { mood = "ACTIVE "; moodLvl = 2; }
  else if (normalizedScore < 75) { mood = "EXCITED"; moodLvl = 3; }
  else                       { mood = "CHAOTIC"; moodLvl = 4; }

  // 6. Visual/Audio Output
  updateHardware(moodLvl);

  // 7. LCD Update
  renderMood(mood, (int)normalizedScore);

  // 8. Data Logging
  Serial.print("[DATA] Avg:"); Serial.print(avgEnergy);
  Serial.print(" | Var:"); Serial.print(variance);
  Serial.print(" | Score:"); Serial.print(crowdScore);
  Serial.print(" | Norm:"); Serial.print(normalizedScore);
  Serial.print(" | Peak:"); Serial.println(peakScore);
  
  delay(50);
}

void updateHardware(int level) {
  digitalWrite(LED_CALM,    level == 1 ? HIGH : LOW);
  digitalWrite(LED_ACTIVE,  level == 2 ? HIGH : LOW);
  digitalWrite(LED_EXCITED, level == 3 ? HIGH : LOW);
  digitalWrite(LED_CHAOTIC, level == 4 ? HIGH : LOW);

  if (level == 4) {
    tone(BUZZER_PIN, 1000);
  } else {
    noTone(BUZZER_PIN);
  }
}

void renderMood(String mood, int scorePercent) {
  lcd.setCursor(0, 0);
  lcd.print("MOOD: " + mood);

  lcd.setCursor(0, 1);
  int meterWidth = map(constrain(scorePercent, 0, 100), 0, 100, 0, 15);
  for (int i = 0; i < 16; i++) {
    if (i <= meterWidth) lcd.write((uint8_t)map(meterWidth, 0, 15, 0, 7));
    else lcd.print(" ");
  }
}

int moodToLevel(String mood) {
  mood.trim();
  mood.toUpperCase();
  if (mood == "CALM") return 1;
  if (mood == "ACTIVE") return 2;
  if (mood == "EXCITED") return 3;
  if (mood == "CHAOTIC") return 4;
  return 1;
}

void handleSerialInput() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;

    if (c != '\n') {
      serialLine += c;
      if (serialLine.length() > 120) serialLine = "";
      continue;
    }

    String line = serialLine;
    serialLine = "";
    line.trim();
    if (line.length() == 0) continue;

    // Supported formats:
    // 1) MOOD:CALM
    // 2) MOOD:EXCITED,SCORE:72
    int moodPos = line.indexOf("MOOD:");
    if (moodPos < 0) continue;

    int moodStart = moodPos + 5;
    int commaPos = line.indexOf(",", moodStart);
    String moodToken = (commaPos >= 0) ? line.substring(moodStart, commaPos) : line.substring(moodStart);
    moodToken.trim();
    moodToken.toUpperCase();

    int parsedLevel = moodToLevel(moodToken);
    String paddedMood = moodToken;
    while (paddedMood.length() < 7) paddedMood += " ";

    int score = serialScore;
    int scorePos = line.indexOf("SCORE:");
    if (scorePos >= 0) {
      int scoreStart = scorePos + 6;
      String scoreToken = line.substring(scoreStart);
      scoreToken.trim();
      score = constrain(scoreToken.toInt(), 0, 100);
    }

    serialMood = paddedMood;
    serialMoodLvl = parsedLevel;
    serialScore = score;
    serialOverride = true;
    lastSerialMsgMs = millis();
  }
}
