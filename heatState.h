#ifndef stateMachine_h
#define stateMachine_h
#include <RTCZero.h>


// Port definitions
#define ONE_WIRE_BUS MYSX_A1
#define HEAT_VALVE   MYSX_D3_INT
#define CIRC_PUMP    MYSX_D4_INT

const uint8_t TEMP_FLOOR     = 0;
const uint8_t TEMP_INLET     = 1;
const uint8_t TEMP_OUTLET    = 2;
const uint8_t TEMP_HOT_WATER = 3;

const uint8_t FETCH_HOT_WATER = 5;
const uint8_t VALVE_SWITCH    = 6;
const uint8_t PUMP_SWITCH     = 7;
const uint8_t SUMMER          = 8;
// temperature sensors (relay sensors are defined in heatState)

// Interval between we should check if we should let hot water into the system
#define COOLING_CHECK_INTERVAL      30    // minutes
#define PUMP_RUN_TIME_BEFORE_CHECK  3     // minutes
#define HYSTERISIS 0.5

struct heatState {
  void(*Transition)();
  void(*Update)();
  const char* name;
};


// definition of the heat state machine : state & properties
typedef struct {
  heatState* currentState;
  float floorTemp;
  float inletTemp;
  float floorTarget;
  float inletTarget;
  uint32_t stateEnter;
  uint32_t stateEnterMinutes;
  RTCZero* rtc;
  bool valve;
  bool pump;
} heatSM;

void init(RTCZero* rtc, void(*sendCallback)(uint8_t, bool));

void heatSwitchSM(heatState& newState);      // Change the state in the machine
void heatUpdateSM();                         // Update the state machine (transition once, then update) etc.
uint32_t heatTimeInState();                  // Time elapsed in state (in seconds!)
bool heatCurrentStateIs(heatState& state);
void fetchHotWater();
void setFloorThreshold(float temperature);
float getFloorThreshold();
void setHotWaterThreshold(float temperature);
float getHotWaterThreshold();
void summer(bool enable);
void currentTemperature(uint8_t sensor, float temperature);
void setValve(bool state);
void setPump(bool state);

/********************************************************************
 * states / state transitions below here, should not be called outside
 * the statemachine.
 */
void HeatingTransition();
void Heating();
void HeatCheckTransition();
void HeatCheck();
void CoolingTransition();
void Cooling();
void CheckFloorTempWhileCooling();
void CheckFloorTempWhileCoolingTransition();
void WaitForHotWater();
void WaitForHotWaterTransition();
void SummerMode();
void SummerModeTransition();

#endif
