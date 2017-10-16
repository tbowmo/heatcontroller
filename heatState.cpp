#include <Arduino.h>
#include "heatState.h"

static heatSM _heatSM;

void (*_sendStateCallback)(uint8_t, bool);

void heatSwitchSM(heatState& newState) {
  Serial.print("old state ");
  Serial.print(_heatSM.currentState->name);
  Serial.print(" time in state ");
  Serial.println(heatTimeInState());
  Serial.print("New state ");
  Serial.println(newState.name);
  
  // Change state if needed
  if (_heatSM.currentState != &newState) _heatSM.currentState = &newState;
  // Transition event
  if (_heatSM.currentState->Transition) _heatSM.currentState->Transition();
  // save time
  _heatSM.stateEnter = _heatSM.rtc->getEpoch();
}

bool heatCurrentStateIs(heatState& state) {
  return _heatSM.currentState ==  &state;
}

uint32_t heatMinutesInState() {
  return heatTimeInState() / 60;
}

uint32_t heatTimeInState() {
  return _heatSM.rtc->getEpoch() - _heatSM.stateEnter;
}

void heatUpdateSM() {
  if (_heatSM.currentState->Update) _heatSM.currentState->Update();
}

/**********************************/
static heatState shHeating         = { HeatingTransition, Heating, "Heating"};
static heatState shHeatCheck       = { HeatCheckTransition, HeatCheck, "Check heating"};
static heatState shCooling         = { CoolingTransition, Cooling, "Cooling"};
static heatState shCheckFloorTemp  = { CheckFloorTempWhileCoolingTransition, CheckFloorTempWhileCooling, "CheckFloor"};
static heatState shWaitForHotWater = { WaitForHotWaterTransition, WaitForHotWater, "WaitHotWatter"};
static heatState shSummer          = { SummerModeTransition, SummerMode, "Summer"};
/**********************************/

/********* States *************/
void HeatingTransition() {
  setValve(true);
  setPump(true);
}

void Heating() {
  if(_heatSM.floorTemp > (_heatSM.floorTarget + HYSTERISIS)) {
    heatSwitchSM(shHeatCheck);
  }
}

void HeatCheckTransition() {
  setValve(false);
  setPump(true);
}

void HeatCheck() {
  if (heatMinutesInState() > PUMP_RUN_TIME_BEFORE_CHECK) {
    if(_heatSM.floorTemp > (_heatSM.floorTarget)) {
      heatSwitchSM(shCooling);
    } else {
      heatSwitchSM(shHeating);
    }
  }
}

void CoolingTransition() {
  setValve(false);
  setPump(false);
}

void Cooling() {
  if ((_heatSM.rtc->getMinutes() % COOLING_CHECK_INTERVAL) == 0) {
    heatSwitchSM(shCheckFloorTemp);
  }
}

void CheckFloorTempWhileCoolingTransition() {
  setPump(true);
}

void CheckFloorTempWhileCooling() {
  if (heatMinutesInState() > PUMP_RUN_TIME_BEFORE_CHECK) {
    if(_heatSM.floorTemp < (_heatSM.floorTarget - HYSTERISIS)) {
      heatSwitchSM(shHeating);
    } else {
      heatSwitchSM(shCooling);
    }
  }
}

void WaitForHotWater() {
  if (_heatSM.inletTemp > _heatSM.inletTarget) {
    digitalWrite(LED_YELLOW, LOW);
    setValve(false);
    _sendStateCallback(FETCH_HOT_WATER, false);
    if (_heatSM.summer) {
      heatSwitchSM(shSummer);
    } else {
      heatSwitchSM(shCooling);
    }
  }
}

void WaitForHotWaterTransition() {
  digitalWrite(LED_YELLOW, HIGH);
  setValve(true);
  _sendStateCallback(FETCH_HOT_WATER, true);
}

void SummerModeTransition() {
  setValve(false);
  setPump(false);
}

void SummerMode() {
  // Do nothing, as summer mode is powered off
}

/**
 * "public" methods
 **/
void fetchHotWater() {
  if (!heatCurrentStateIs(shHeating)) {
    heatSwitchSM(shWaitForHotWater);
  }
}

void setFloorThreshold(float temperature) {
  _heatSM.floorTarget = temperature;
}

void setHotwaterThreshold(float temperature) {
  _heatSM.inletTarget = temperature;
}

void setValve(bool state) {
  if (_heatSM.valve != state) {
    _heatSM.valve = state;
    digitalWrite(HEAT_VALVE, state); // Turn on heat valve
    _sendStateCallback(VALVE_SWITCH, state);
  }
}

void setPump(bool state) {
  if (_heatSM.pump != state) {
    _heatSM.pump = state;
    digitalWrite(CIRC_PUMP, state);
    _sendStateCallback(PUMP_SWITCH, state);
  }
}

void init(RTCZero* rtc, void(*sendCallback)(uint8_t, bool)) {
  _heatSM.rtc = rtc;
  _sendStateCallback = sendCallback;
  heatSwitchSM(shHeating);
}

void summer(bool state) {
  _heatSM.summer = state;
  if (state) {
    heatSwitchSM(shSummer);
  } else {
    heatSwitchSM(shHeating);
  }
  _sendStateCallback(SUMMER, state);
}

void currentTemperature(uint8_t sensor, float temperature) {
  if (sensor == TEMP_HEATLOOP) {
    _heatSM.floorTemp = temperature;
  }
  if (sensor == TEMP_INLET) {
    _heatSM.inletTemp = temperature;
  }
}

