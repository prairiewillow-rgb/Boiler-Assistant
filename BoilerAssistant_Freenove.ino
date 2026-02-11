/*
============================================================
  Boiler Assistant "Freenove" v1.0
============================================================
*/

#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>
#include <Adafruit_MAX31855.h>
#include <EEPROM.h>
#include "Pinout.h"

// Forward declarations (ui.cpp)
void ui_showHome(double tF, int fanPercent);
void ui_showScreen(int state, double tF, int fanPercent);

// ---------------------------------------------------------
// PIN ALIASES
// ---------------------------------------------------------
#define FAN_PIN        PIN_FAN_PWM
#define DAMPER_PIN     PIN_DAMPER_RELAY
#define FAN_RELAY_PIN  PIN_FAN_RELAY   // active-LOW relay on A1

// ---------------------------------------------------------
// LCD + MAX31855
// ---------------------------------------------------------
#define LCD_I2C_ADDRESS 0x27
LiquidCrystal_PCF8574 lcd(LCD_I2C_ADDRESS);

Adafruit_MAX31855 tc(PIN_MAX31855_SCK, PIN_TC1_CS, PIN_MAX31855_MISO);

// ---------------------------------------------------------
// KEYPAD PINS
// ---------------------------------------------------------
#define R1 PIN_KEYPAD_ROW1
#define R2 PIN_KEYPAD_ROW2
#define R3 PIN_KEYPAD_ROW3
#define R4 PIN_KEYPAD_ROW4

#define C1 PIN_KEYPAD_COL1
#define C2 PIN_KEYPAD_COL2
#define C3 PIN_KEYPAD_COL3
#define C4 PIN_KEYPAD_COL4

const char KEYS[4][4] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// ---------------------------------------------------------
// UNO R4 KEYPAD ENGINE
// ---------------------------------------------------------
char lastKey = 0;

char scanKeypad() {
  const int rows[4] = {R1, R2, R3, R4};
  const int cols[4] = {C1, C2, C3, C4};

  for (int c = 0; c < 4; c++)
    pinMode(cols[c], INPUT_PULLUP);

  char found = 0;

  for (int r = 0; r < 4; r++) {
    for (int i = 0; i < 4; i++) {
      pinMode(rows[i], INPUT);
      digitalWrite(rows[i], LOW);
    }

    pinMode(rows[r], OUTPUT);
    digitalWrite(rows[r], LOW);

    delayMicroseconds(800);

    for (int c = 0; c < 4; c++) {
      if (digitalRead(cols[c]) == LOW) {
        delayMicroseconds(400);
        if (digitalRead(cols[c]) == LOW)
          found = KEYS[r][c];
      }
    }
  }

  for (int i = 0; i < 4; i++) {
    pinMode(rows[i], INPUT);
    digitalWrite(rows[i], LOW);
  }

  return found;
}

char getKey() {
  static char stableKey = 0;
  static unsigned long lastChange = 0;

  char k = scanKeypad();
  unsigned long now = millis();

  if (k == 0) {
    stableKey = 0;
    lastKey = 0;
    return 0;
  }

  if (k != stableKey) {
    stableKey = k;
    lastChange = now;
    return 0;
  }

  if (now - lastChange < 40)
    return 0;

  if (k == lastKey)
    return 0;

  lastKey = k;
  return k;
}

// ---------------------------------------------------------
// UI STATE MACHINE
// ---------------------------------------------------------
enum UIState {
  UI_HOME,
  UI_SETPOINT,
  UI_BURNLOGIC,
  UI_BOOSTTIME,
  UI_SYSTEM,
  UI_DEADBAND,
  UI_PID_PROFILE,
  UI_PID_PARAM,
  UI_PID_EDIT,
  UI_ADAPTIVE_DIAG,
  UI_CLAMP_MENU,
  UI_CLAMP_MIN,
  UI_CLAMP_MAX
};

UIState uiState = UI_HOME;
bool uiNeedRedraw = true;

// ---------------------------------------------------------
// SETTINGS + EEPROM
// ---------------------------------------------------------
int exhaustSetpoint   = 450;
int burnLogicMode     = 0;
int boostTimeSeconds  = 60;
int deadbandF         = 100;

float pidBelowKp = 1.50, pidBelowKi = 0.10, pidBelowKd = 0.00;
float pidNormKp  = 1.00, pidNormKi  = 0.05, pidNormKd  = 0.00;
float pidAboveKp = 2.00, pidAboveKi = 0.20, pidAboveKd = 0.00;

float adaptiveSlope = 1.0;

int clampMinPercent = 40;
int clampMaxPercent = 100;

bool fanLowOffMode = false;

unsigned long burnBoostStart = 0;
enum BurnState { BURN_BOOST, BURN_ADAPTIVE };
BurnState burnState = BURN_BOOST;

// UI edit buffers
String newSetpointValue   = "";
String boostTimeEditValue = "";
String deadbandEditValue  = "";
String pidEditValue       = "";
String clampMinEditValue  = "";
String clampMaxEditValue  = "";

int burnLogicSelected = 0;
int pidProfileSelected = 0;
int pidParamSelected = 0;

// Adaptive learning state
double lastT = 0;
double lastRate = 0;
unsigned long lastTtime = 0;

// ---------------------------------------------------------
// EEPROM helpers
// ---------------------------------------------------------
void saveInt(int addr, int v){ EEPROM.put(addr, v); }
int loadInt(int addr, int def){
  int v; EEPROM.get(addr, v);
  return v < 0 || v > 10000 ? def : v;
}

void saveFloat(int addr, float v){ EEPROM.put(addr, v); }
float loadFloat(int addr, float def){
  float v; EEPROM.get(addr, v);
  return isnan(v) ? def : v;
}

// ---------------------------------------------------------
// CACHED SENSOR READ — once per second
// ---------------------------------------------------------
double exhaust_readF_cached(){
  static double lastF = -1;
  static unsigned long lastRead = 0;
  unsigned long now = millis();

  if(now - lastRead >= 1000){
    double tC = tc.readCelsius();
    if(!isnan(tC)){
      lastF = tC * 9.0 / 5.0 + 32.0;
    }
    lastRead = now;
  }
  return lastF;
}

// ---------------------------------------------------------
// DISPLAY-ONLY SMOOTHING (3%)
// ---------------------------------------------------------
double smoothExhaustF(double raw){
  static double filtered = 0;
  if(filtered == 0 || raw < 0) filtered = raw;
  filtered = (filtered * 0.97) + (raw * 0.03);
  return filtered;
}

// ---------------------------------------------------------
// BOOT SCREEN 
// ---------------------------------------------------------
void showBootScreen() {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("  BOILER ASSISTANT  ");
  delay(400);
  lcd.setCursor(0,1); lcd.print("    Initializing    ");
  delay(400);

  const char* bar[] = {
    "                    ","#                   ","##                  ",
    "###                 ","####                ","#####               ",
    "######              ","#######             ","########            ",
    "#########           ","##########          ","###########         ",
    "############        ","#############       ","##############      ",
    "###############     ","################    ","#################   ",
    "##################  ","################### ","********************"
  };

  for(int i=0;i<21;i++){
    lcd.setCursor(0,2);
    lcd.print(bar[i]);
    delay(80);
  }

  const char spin[4] = {'|','/','-','\\'};
  for(int i=0;i<12;i++){
    lcd.setCursor(0,3);
    lcd.print("System Check ");
    lcd.print(spin[i%4]);
    delay(120);
  }

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("  Boiler Assistant  ");
  lcd.setCursor(0,1); lcd.print("    We don't just   ");
  lcd.setCursor(0,2); lcd.print("    command fire    ");
  lcd.setCursor(0,3); lcd.print("   WE DOMINATE IT!  ");
  delay(2000);
}


// ---------------------------------------------------------
// SETUP
// ---------------------------------------------------------
void setup(){
  Wire.begin();
  lcd.begin(20,4);
  lcd.setBacklight(255);

  pinMode(FAN_PIN, OUTPUT);
  pinMode(DAMPER_PIN, OUTPUT);
  pinMode(FAN_RELAY_PIN, OUTPUT);

  digitalWrite(DAMPER_PIN, LOW);
  digitalWrite(FAN_RELAY_PIN, LOW); // active-LOW → ON

  exhaustSetpoint  = loadInt(0, 450);
  burnLogicMode    = loadInt(10, 0);
  boostTimeSeconds = loadInt(12, 60);
  deadbandF        = loadInt(14, 100);

  pidBelowKp = loadFloat(40, 1.50);
  pidBelowKi = loadFloat(44, 0.10);
  pidBelowKd = loadFloat(48, 0.00);

  pidNormKp = loadFloat(52, 1.00);
  pidNormKi = loadFloat(56, 0.05);
  pidNormKd = loadFloat(60, 0.00);

  pidAboveKp = loadFloat(64, 2.00);
  pidAboveKi = loadFloat(68, 0.20);
  pidAboveKd = loadFloat(72, 0.00);

  adaptiveSlope = loadFloat(100, 1.0);

  clampMinPercent = loadInt(110, 40);
  clampMaxPercent = loadInt(112, 100);

  fanLowOffMode = loadInt(120, 0);

  burnBoostStart = millis();
  burnState = BURN_BOOST;

  uiState = UI_HOME;
  uiNeedRedraw = true;

  showBootScreen();
}
// ---------------------------------------------------------
// PID ENGINE
// ---------------------------------------------------------
float pidIntegral = 0;
float pidLastError = 0;
unsigned long pidLastTime = 0;

float pid_compute(float setpoint, float measured,
                  float Kp, float Ki, float Kd){
  unsigned long now = millis();
  if(pidLastTime == 0){
    pidLastTime = now;
    pidLastError = setpoint - measured;
  }

  float dt = (now - pidLastTime) / 1000.0;
  if(dt <= 0) dt = 0.1;

  float error = setpoint - measured;
  pidIntegral += error * dt;
  if(pidIntegral > 1000) pidIntegral = 1000;
  if(pidIntegral < -1000) pidIntegral = -1000;

  float derivative = (error - pidLastError) / dt;
  float output = Kp * error + Ki * pidIntegral + Kd * derivative;

  pidLastError = error;
  pidLastTime  = now;

  if(output < 0) output = 0;
  if(output > 100) output = 100;
  return output;
}

// ---------------------------------------------------------
// FAN UPDATE — HARD-OFF + DEADBAND RE-ENABLE + 60s MIN INTERVAL
// ---------------------------------------------------------
int fan_update(double tF){
  unsigned long now = millis();
  if(tF < 0) tF = 0;

  int fanPercent = 0;

  // BOOST MODE
  if(burnState == BURN_BOOST){
    if(now - burnBoostStart < (unsigned long)boostTimeSeconds * 1000UL){
      fanPercent = 100;
    } else {
      burnState = BURN_ADAPTIVE;
    }
  }

  // ADAPTIVE / PID
  if(burnState == BURN_ADAPTIVE){
    float lowBand  = exhaustSetpoint - deadbandF;
    float highBand = exhaustSetpoint + deadbandF;

    if(burnLogicMode == 0){
      // ADAPTIVE
      if(tF < lowBand){
        fanPercent = 100;
      }
      else if(tF > highBand){
        fanPercent = 0;
      }
      else {
        float base = 100.0 * (1.0 - (tF - lowBand) / (highBand - lowBand));
        float adj  = adaptiveSlope * base;
        if(adj > 100) adj = 100;
        if(adj < 0)   adj = 0;
        fanPercent = adj;
      }
    }
    else {
      // PID
      if(tF < lowBand){
        fanPercent = 100;
        pidIntegral = 0;
      }
      else if(tF > highBand){
        fanPercent = 0;
        pidIntegral = 0;
      }
      else {
        fanPercent = pid_compute(exhaustSetpoint, tF,
                                 pidNormKp, pidNormKi, pidNormKd);
      }
    }

    // RELAY HARD-OFF + DEADBAND RE-ENABLE + 60s MIN INTERVAL
    static bool relayIsOn = true;
    static unsigned long relayLastChange = 0;
    const unsigned long RELAY_MIN_INTERVAL = 60000; // 60 seconds

    if(fanLowOffMode){
      // REQUEST OFF: fanPercent below clampMin
      if(fanPercent < clampMinPercent){
        if(relayIsOn && (now - relayLastChange > RELAY_MIN_INTERVAL)){
          relayIsOn = false;
          relayLastChange = now;
        }
      }

      // REQUEST ON: fire below deadband
      if(!relayIsOn && tF < (exhaustSetpoint - deadbandF)){
        if(now - relayLastChange > RELAY_MIN_INTERVAL){
          relayIsOn = true;
          relayLastChange = now;
        }
      }
    }
    else {
      // Low Fan Mode OFF → relay always ON
      relayIsOn = true;
    }

    // Drive relay (active-low)
    digitalWrite(FAN_RELAY_PIN, relayIsOn ? LOW : HIGH);

    // If relay is OFF, force fanPercent to 0 for UI
    if(!relayIsOn){
      fanPercent = 0;
    }

    // Clamp max
    if(fanPercent > clampMaxPercent){
      fanPercent = clampMaxPercent;
    }
  }

// Enforce clampMinPercent when NOT using hard-off mode
if(!fanLowOffMode){
    if(fanPercent > 0 && fanPercent < clampMinPercent){
        fanPercent = clampMinPercent;
    }
}

  // PWM OUTPUT
  int pwm = map(fanPercent, 0, 100, 0, 255);
  analogWrite(FAN_PIN, pwm);

  return fanPercent;
}
// ---------------------------------------------------------
// LOOP — 1 SECOND CONTROL TICK + SMOOTH UI
// ---------------------------------------------------------
void loop(){
  static unsigned long lastControl = 0;
  unsigned long now = millis();

  // HANDLE KEYPAD
  char k = getKey();
  if(k){
    uiNeedRedraw = true;
    switch(uiState){

      case UI_HOME:
        if(k=='A'){ uiState=UI_SETPOINT; newSetpointValue=""; }
        else if(k=='B'){ uiState=UI_BURNLOGIC; burnLogicSelected=burnLogicMode; }
        else if(k=='C'){ uiState=UI_PID_PROFILE; }
        else if(k=='D'){ uiState=UI_SYSTEM; }
        break;

      case UI_SETPOINT:
        if(k>='0'&&k<='9'&&newSetpointValue.length()<3)
          newSetpointValue+=k;
        else if(k=='#'){
          int v = newSetpointValue.length() ? newSetpointValue.toInt() : exhaustSetpoint;

// Allow 200–999°F
if(v < 200) v = 200;
if(v > 999) v = 999;

exhaustSetpoint = v;
saveInt(0, v);

uiState = UI_HOME;

        }
        else if(k=='*') uiState=UI_HOME;
        break;

      case UI_BURNLOGIC:
        if(k=='1') burnLogicSelected=0;
        else if(k=='2') burnLogicSelected=1;
        else if(k=='3'){ uiState=UI_BOOSTTIME; boostTimeEditValue=""; break; }
        else if(k=='#'){
          burnLogicMode=burnLogicSelected;
          saveInt(10,burnLogicMode);
          uiState=UI_HOME;
        }
        else if(k=='*') uiState=UI_HOME;
        break;

      case UI_BOOSTTIME:
        if(k>='0'&&k<='9'&&boostTimeEditValue.length()<3)
          boostTimeEditValue+=k;
        else if(k=='#'){
          int v=boostTimeEditValue.length()?boostTimeEditValue.toInt():boostTimeSeconds;
          if(v<10)v=10;
          if(v>300)v=300;
          boostTimeSeconds=v;
          saveInt(12,v);
          uiState=UI_BURNLOGIC;
        }
        else if(k=='*') uiState=UI_BURNLOGIC;
        break;

      case UI_SYSTEM:
        if(k=='1'){ uiState=UI_DEADBAND; deadbandEditValue=""; }
        else if(k=='2'){ uiState=UI_ADAPTIVE_DIAG; }
        else if(k=='3'){ uiState=UI_CLAMP_MENU; }
        else if(k=='*'){ uiState=UI_HOME; }
        break;

      case UI_DEADBAND:
        if(k>='0'&&k<='9'&&deadbandEditValue.length()<3)
          deadbandEditValue+=k;
        else if(k=='#'){
          int v=deadbandEditValue.length()?deadbandEditValue.toInt():deadbandF;
          if(v<10)v=10;
          if(v>200)v=200;
          deadbandF=v;
          saveInt(14,v);
          uiState=UI_SYSTEM;
        }
        else if(k=='*') uiState=UI_SYSTEM;
        break;

      case UI_CLAMP_MENU:
        if(k=='1'){ uiState=UI_CLAMP_MIN; clampMinEditValue=""; }
        else if(k=='2'){ uiState=UI_CLAMP_MAX; clampMaxEditValue=""; }
        else if(k=='4'){
          fanLowOffMode = !fanLowOffMode;
          saveInt(120, fanLowOffMode ? 1 : 0);
        }
        else if(k=='*'){ uiState=UI_SYSTEM; }
        break;

      case UI_CLAMP_MIN:
        if(k>='0'&&k<='9'&&clampMinEditValue.length()<3)
          clampMinEditValue+=k;
        else if(k=='#'){
          int v=clampMinEditValue.length()?clampMinEditValue.toInt():clampMinPercent;
          if(v<0)v=0;
          if(v>100)v=100;
          clampMinPercent=v;
          if(clampMinPercent>clampMaxPercent)
            clampMaxPercent=clampMinPercent;
          saveInt(110,clampMinPercent);
          saveInt(112,clampMaxPercent);
          uiState=UI_CLAMP_MAX;
          clampMaxEditValue="";
        }
        else if(k=='*') uiState=UI_CLAMP_MENU;
        break;

      case UI_CLAMP_MAX:
        if(k>='0'&&k<='9'&&clampMaxEditValue.length()<3)
          clampMaxEditValue+=k;
        else if(k=='#'){
          int v=clampMaxEditValue.length()?clampMaxEditValue.toInt():clampMaxPercent;
          if(v<0)v=0;
          if(v>100)v=100;
          clampMaxPercent=v;
          if(clampMinPercent>clampMaxPercent)
            clampMinPercent=clampMaxPercent;
          saveInt(110,clampMinPercent);
          saveInt(112,clampMaxPercent);
          uiState=UI_CLAMP_MENU;
        }
        else if(k=='*') uiState=UI_CLAMP_MENU;
        break;

      case UI_PID_PROFILE:
        if(k=='1'){ pidProfileSelected=1; uiState=UI_PID_PARAM; }
        else if(k=='2'){ pidProfileSelected=2; uiState=UI_PID_PARAM; }
        else if(k=='3'){ pidProfileSelected=3; uiState=UI_PID_PARAM; }
        else if(k=='*'){ uiState=UI_HOME; }
        break;

      case UI_PID_PARAM:
        if(k=='1'){ pidParamSelected=1; pidEditValue=""; uiState=UI_PID_EDIT; }
        else if(k=='2'){ pidParamSelected=2; pidEditValue=""; uiState=UI_PID_EDIT; }
        else if(k=='3'){ pidParamSelected=3; pidEditValue=""; uiState=UI_PID_EDIT; }
        else if(k=='*'){ uiState=UI_PID_PROFILE; }
        break;

      case UI_PID_EDIT:
        if((k>='0'&&k<='9') || k=='D' || k=='.'){
          char c = (k=='D') ? '.' : k;
          if(c=='.' && pidEditValue.indexOf('.')!=-1){
          } else if(pidEditValue.length() < 7){
            pidEditValue += c;
          }
        }
        else if(k=='#'){
          float v = pidEditValue.length() ? pidEditValue.toFloat() : 0.0;

          if(pidProfileSelected==1){
            if(pidParamSelected==1) pidBelowKp=v;
            else if(pidParamSelected==2) pidBelowKi=v;
            else pidBelowKd=v;
          }
          else if(pidProfileSelected==2){
            if(pidParamSelected==1) pidNormKp=v;
            else if(pidParamSelected==2) pidNormKi=v;
            else pidNormKd=v;
          }
          else {
            if(pidParamSelected==1) pidAboveKp=v;
            else if(pidParamSelected==2) pidAboveKi=v;
            else pidAboveKd=v;
          }

          EEPROM.put(40,pidBelowKp);
          EEPROM.put(44,pidBelowKi);
          EEPROM.put(48,pidBelowKd);
          EEPROM.put(52,pidNormKp);
          EEPROM.put(56,pidNormKi);
          EEPROM.put(60,pidNormKd);
          EEPROM.put(64,pidAboveKp);
          EEPROM.put(68,pidAboveKi);
          EEPROM.put(72,pidAboveKd);

          uiState = UI_PID_PARAM;
        }
        else if(k=='*'){
          uiState = UI_PID_PARAM;
        }
        break;

      case UI_ADAPTIVE_DIAG:
        if(k=='#'){
          adaptiveSlope = 1.0;
          EEPROM.put(100, adaptiveSlope);
        }
        else if(k=='*'){
          uiState = UI_SYSTEM;
        }
        break;

      default:
        uiState = UI_HOME;
        break;
    }
  }

  // CONTROL LOGIC ONCE PER SECOND
  if(now - lastControl < 1000){
    return;
  }
  lastControl = now;

  // SENSOR READ
  double rawTF = exhaust_readF_cached();

  // ADAPTIVE LEARNING
  double dt = (lastTtime==0)?0:(now-lastTtime)/1000.0;
  double dT = rawTF - lastT;
  if(dt>0 && rawTF>0) lastRate = dT/dt;
  lastT = rawTF;
  lastTtime = now;

  if(burnState==BURN_ADAPTIVE && burnLogicMode==0 && rawTF>0){
    if(rawTF < exhaustSetpoint-20 && lastRate < 0.05) adaptiveSlope += 0.005;
    if(rawTF > exhaustSetpoint+20 && lastRate > 0.05) adaptiveSlope -= 0.005;
    if(adaptiveSlope<0.5) adaptiveSlope=0.5;
    if(adaptiveSlope>2.0) adaptiveSlope=2.0;
  }

  // SMOOTH TEMP
  double smoothTF = smoothExhaustF(rawTF);

  // DISPLAY DEAD-BAND (2°F)
  static int lastDisplayTF = -999;
  int displayTF = (int)(smoothTF + 0.5);
  if(lastDisplayTF != -999 && abs(displayTF - lastDisplayTF) < 2){
    displayTF = lastDisplayTF;
  } else {
    lastDisplayTF = displayTF;
  }

  // FAN UPDATE
  int fanPercent = fan_update(rawTF);

  // FAN DISPLAY SMOOTHING
  static int lastFanDisplay = -1;
  int fanDisplay = fanPercent;
  if(fanPercent == 0){
    lastFanDisplay = 0;
    fanDisplay = 0;
  } else {
    if(lastFanDisplay < 0){
      lastFanDisplay = fanPercent;
    } else {
      lastFanDisplay = (lastFanDisplay * 0.80) + (fanPercent * 0.20);
    }
    fanDisplay = lastFanDisplay;
  }

  // SAFETY OUTPUTS
  digitalWrite(DAMPER_PIN, LOW);

  // UI UPDATE
  if(uiState == UI_HOME){
    ui_showHome(displayTF, fanDisplay);
  } else {
    ui_showScreen(uiState, displayTF, fanDisplay);
  }
}
