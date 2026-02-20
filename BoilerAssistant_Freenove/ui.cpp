#include <Arduino.h>
#include <LiquidCrystal_PCF8574.h>

// LCD instance from main .ino
extern LiquidCrystal_PCF8574 lcd;

// External variables from main .ino
extern int exhaustSetpoint;
extern int burnLogicMode;
extern int deadbandF;
extern int boostTimeSeconds;
extern int clampMinPercent;
extern int clampMaxPercent;
extern float adaptiveSlope;
extern float pidBelowKp, pidBelowKi, pidBelowKd;
extern float pidNormKp,  pidNormKi,  pidNormKd;
extern float pidAboveKp, pidAboveKi, pidAboveKd;

extern bool fanLowOffMode;   // ⭐ NEW FEATURE

extern String newSetpointValue;
extern String boostTimeEditValue;
extern String deadbandEditValue;
extern String pidEditValue;
extern String clampMinEditValue;
extern String clampMaxEditValue;

extern int burnLogicSelected;
extern int pidProfileSelected;
extern int pidParamSelected;

// ---------------------------------------------------------
// LCD FULL-SCREEN RENDERER
// ---------------------------------------------------------
void lcd4(const char* l1, const char* l2, const char* l3, const char* l4) {
  const char* lines[4] = { l1, l2, l3, l4 };
  for (int i = 0; i < 4; i++) {
    lcd.setCursor(0, i);
    lcd.print("                    ");
    lcd.setCursor(0, i);
    lcd.print(lines[i]);
  }
}

// ---------------------------------------------------------
// HOME SCREEN
// ---------------------------------------------------------
void ui_showHome(double tF, int fanPercent){
  char l1[21], l2[21], l3[21], l4[21];

  snprintf(l1, 21, "Exh Set: %3dF", exhaustSetpoint);

  if(tF < 0) snprintf(l2, 21, "Exh Cur: ----F");
  else       snprintf(l2, 21, "Exh Cur: %3dF", (int)(tF + 0.5));

  snprintf(l3, 21, "Fan: %3d%%", fanPercent);

  if(fanPercent == 100)
    snprintf(l4, 21, "BOOSTING");
  else if(burnLogicMode == 0)
    snprintf(l4, 21, "Mode: ADAPTIVE");
  else
    snprintf(l4, 21, "Mode: PID");

  lcd4(l1, l2, l3, l4);
}

// ---------------------------------------------------------
// SETPOINT SCREEN
// ---------------------------------------------------------
void ui_showSetpoint(){
  char l1[21], l2[21], l3[21], l4[21];

  snprintf(l1,21,"EXHAUST SET POINT ");
  snprintf(l2,21,"Current: %3dF", exhaustSetpoint);
  snprintf(l3,21,"New: %s", newSetpointValue.c_str());
  snprintf(l4,21,"*=CANCEL   #=SAVE ");

  lcd4(l1,l2,l3,l4);
}

// ---------------------------------------------------------
// BURN LOGIC MENU
// ---------------------------------------------------------
void ui_showBurnLogic(){
  char l1[21], l2[21], l3[21], l4[21];

  snprintf(l1,21,"BURN LOGIC MODE ");
  snprintf(l2,21,"1: ADAPTIVE%s", (burnLogicSelected==0)?" <":"");
  snprintf(l3,21,"2: PID%s", (burnLogicSelected==1)?" <":"");
  snprintf(l4,21,"3: BOOST TIME #Save");

  lcd4(l1,l2,l3,l4);
}

// ---------------------------------------------------------
// BOOST TIME EDIT
// ---------------------------------------------------------
void ui_showBoostTime(){
  char l1[21], l2[21], l3[21], l4[21];

  snprintf(l1,21,"BOOST TIME (sec) ");
  snprintf(l2,21,"Current: %3d", boostTimeSeconds);
  snprintf(l3,21,"New: %s", boostTimeEditValue.c_str());
  snprintf(l4,21,"*=CANCEL   #=SAVE ");

  lcd4(l1,l2,l3,l4);
}

// ---------------------------------------------------------
// SYSTEM MENU
// ---------------------------------------------------------
void ui_showSystem(){
  lcd4(
    "SYSTEM SETTINGS  ",
    "1: DEADBAND",
    "2: ADAPTIVE DIAG",
    "3: CLAMP  *=BACK"
  );
}

// ---------------------------------------------------------
// DEADBAND EDIT
// ---------------------------------------------------------
void ui_showDeadband(){
  char l1[21], l2[21], l3[21], l4[21];

  snprintf(l1,21,"DEADBAND (F)    ");
  snprintf(l2,21,"Current: %3d", deadbandF);
  snprintf(l3,21,"New: %s", deadbandEditValue.c_str());
  snprintf(l4,21,"*=CANCEL   #=SAVE ");

  lcd4(l1,l2,l3,l4);
}

// ---------------------------------------------------------
// CLAMP MENU  ⭐ UPDATED
// ---------------------------------------------------------
void ui_showClampMenu(){
  char l1[21], l2[21], l3[21], l4[21];

  snprintf(l1,21,"CLAMP SETTINGS   ");
  snprintf(l2,21,"1:Min: %3d%%", clampMinPercent);
  snprintf(l3,21,"  Max: %3d%%", clampMaxPercent);

  // ⭐ NEW LINE — Fan Off Mode
  snprintf(l4,21,"4:Deadzone Mode<%s>", fanLowOffMode ? "ON" : "OFF");

  lcd4(l1,l2,l3,l4);
}

// ---------------------------------------------------------
// CLAMP MIN EDIT
// ---------------------------------------------------------
void ui_showClampMin(){
  char l1[21], l2[21], l3[21], l4[21];

  snprintf(l1,21,"EDIT CLAMP MIN (%%)");
  snprintf(l2,21,"Current: %3d", clampMinPercent);
  snprintf(l3,21,"New: %s", clampMinEditValue.c_str());
  snprintf(l4,21,"*=CANCEL   #=SAVE ");

  lcd4(l1,l2,l3,l4);
}

// ---------------------------------------------------------
// CLAMP MAX EDIT
// ---------------------------------------------------------
void ui_showClampMax(){
  char l1[21], l2[21], l3[21], l4[21];

  snprintf(l1,21,"EDIT CLAMP MAX (%%)");
  snprintf(l2,21,"Current: %3d", clampMaxPercent);
  snprintf(l3,21,"New: %s", clampMaxEditValue.c_str());
  snprintf(l4,21,"*=CANCEL   #=SAVE ");

  lcd4(l1,l2,l3,l4);
}

// ---------------------------------------------------------
// PID PROFILE MENU
// ---------------------------------------------------------
void ui_showPIDProfile(){
  lcd4(
    "PID TUNING     ",
    "1: BELOW",
    "2: NORMAL",
    "3: ABOVE   *=BACK"
  );
}

// ---------------------------------------------------------
// PID PARAM MENU
// ---------------------------------------------------------
void ui_showPIDParam(){
  char l1[21], l2[21], l3[21], l4[21];
  float kp,ki,kd;
  const char* title;

  if(pidProfileSelected==1){
    title="PID BELOW";
    kp=pidBelowKp; ki=pidBelowKi; kd=pidBelowKd;
  }
  else if(pidProfileSelected==2){
    title="PID NORMAL";
    kp=pidNormKp; ki=pidNormKi; kd=pidNormKd;
  }
  else {
    title="PID ABOVE";
    kp=pidAboveKp; ki=pidAboveKi; kd=pidAboveKd;
  }

  snprintf(l1,21,"%s",title);
  snprintf(l2,21,"1:KP %.3f",kp);
  snprintf(l3,21,"2:KI %.3f",ki);
  snprintf(l4,21,"3:KD %.3f *=BACK",kd);

  lcd4(l1,l2,l3,l4);
}

// ---------------------------------------------------------
// PID EDIT
// ---------------------------------------------------------
void ui_showPIDEdit(){
  char l1[21], l2[21], l3[21], l4[21];

  const char* prof =
    (pidProfileSelected==1) ? "BELOW" :
    (pidProfileSelected==2) ? "NORMAL" :
                              "ABOVE";

  const char* param =
    (pidParamSelected==1) ? "KP" :
    (pidParamSelected==2) ? "KI" :
                            "KD";

  float current;

  if(pidProfileSelected==1){
    current = (pidParamSelected==1) ? pidBelowKp :
              (pidParamSelected==2) ? pidBelowKi :
                                      pidBelowKd;
  }
  else if(pidProfileSelected==2){
    current = (pidParamSelected==1) ? pidNormKp :
              (pidParamSelected==2) ? pidNormKi :
                                      pidNormKd;
  }
  else {
    current = (pidParamSelected==1) ? pidAboveKp :
              (pidParamSelected==2) ? pidAboveKi :
                                      pidAboveKd;
  }

  snprintf(l1,21,"EDIT %s (%s)",param,prof);
  snprintf(l2,21,"Current: %.3f",current);
  snprintf(l3,21,"New: %s",pidEditValue.c_str());
  snprintf(l4,21,"D=DOT *=CANCEL #=SAVE");

  lcd4(l1,l2,l3,l4);
}

// ---------------------------------------------------------
// ADAPTIVE DIAGNOSTICS
// ---------------------------------------------------------
void ui_showAdaptiveDiag(double lastRate){
  char l1[21], l2[21], l3[21], l4[21];

  snprintf(l1,21,"ADAPTIVE LEARNING  ");
  snprintf(l2,21,"Slope: %.2f",adaptiveSlope);
  snprintf(l3,21,"dT/ds: %.3f",lastRate);
  snprintf(l4,21,"*=BACK   #=RESET   ");

  lcd4(l1,l2,l3,l4);
}

// ---------------------------------------------------------
// UI DISPATCHER
// ---------------------------------------------------------
void ui_showScreen(int state, double tF, int fanPercent){
  switch(state){

    case 0:  ui_showHome(tF, fanPercent); break;
    case 1:  ui_showSetpoint(); break;
    case 2:  ui_showBurnLogic(); break;
    case 3:  ui_showBoostTime(); break;
    case 4:  ui_showSystem(); break;
    case 5:  ui_showDeadband(); break;
    case 6:  ui_showPIDProfile(); break;
    case 7:  ui_showPIDParam(); break;
    case 8:  ui_showPIDEdit(); break;
    case 9:  ui_showAdaptiveDiag(0); break;
    case 10: ui_showClampMenu(); break;
    case 11: ui_showClampMin(); break;
    case 12: ui_showClampMax(); break;

    default:
      ui_showHome(tF, fanPercent);
      break;
  }
}
