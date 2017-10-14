#include <Arduino.h>
#include "heatState.h"

static heatSM _heatSM;

void (*_sendStateCallback)(uint8_t, bool);

void heatSwitchSM(heatState& newState) {
  Serial.print("Time in previous state ");
  Serial.println(heatTimeInState());
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

uint32_t heatTimeInState() {
  return _heatSM.rtc->getEpoch() - _heatSM.stateEnter;
}

void heatUpdateSM(float floorTemp, float inletTemp){
  _heatSM.floorTemp = floorTemp;
  _heatSM.inletTemp = inletTemp;
  if (_heatSM.currentState->Update) _heatSM.currentState->Update();
}

/**********************************/
static heatState shHeating         = { HeatingTransition, Heating };
static heatState shCooling         = { CoolingTransition, Cooling };
static heatState shCheckFloorTemp  = { CheckFloorTempWhileCoolingTransition, CheckFloorTempWhileCooling };
static heatState shWaitForHotWater = { WaitForHotWaterTransition, WaitForHotWater };
static heatState shSummer          = { SummerModeTransition, SummerMode };
/**********************************/

/********* States *************/
void HeatingTransition() {
  Serial.print(_heatSM.rtc->getEpoch());
  Serial.println(" Heat transition");
  setValve(true);
  setPump(true);
}

void Heating() {
  if(_heatSM.floorTemp > (_heatSM.floorTarget + HYSTERISIS)) {
    heatSwitchSM(shCooling);
  }
}

void CoolingTransition() {
  Serial.print(_heatSM.rtc->getEpoch());
  Serial.println(" cooling transition");
  setValve(false);
  setPump(false);
}

void Cooling() {
  if ((_heatSM.rtc->getMinutes() % COOLING_CHECK_INTERVAL) == 0) {
    heatSwitchSM(shCheckFloorTemp);
  }
}

void CheckFloorTempWhileCoolingTransition() {
  Serial.print(_heatSM.rtc->getEpoch());
  Serial.println(" Check floor transition");
  setPump(true);
}

void CheckFloorTempWhileCooling() {
  if (heatTimeInState() > 60) {
    if(_heatSM.floorTemp < (_heatSM.floorTarget - HYSTERISIS)) {
      heatSwitchSM(shHeating);
    } else {
      heatSwitchSM(shCooling);
    }
  }
}

void WaitForHotWater() {
  if (_heatSM.inletTemp > _heatSM.hotWaterThreshold) {
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
  Serial.print(_heatSM.rtc->getEpoch());
  Serial.println(" Fetching hot water");
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

void setFloorTemperature(float temperature) {
  _heatSM.floorTarget = temperature;
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

void init(RTCZero* rtc, float temperature, void(*sendCallback)(uint8_t, bool)) {
  _heatSM.floorTarget = temperature;
  _heatSM.hotWaterThreshold = 26;
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

