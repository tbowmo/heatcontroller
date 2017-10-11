#ifndef stateMachine_h
#define stateMachine_h
#include <RTCZero.h>


// Port definitions
#define ONE_WIRE_BUS MYSX_A1
#define HEAT_VALVE   MYSX_D3_INT
#define CIRC_PUMP    MYSX_D4_INT

// Interval between we should check if we should let hot water into the system
#define COOLING_CHECK_INTERVAL 30
#define HYSTERISIS 0.5

struct heatState {
  void(*Transition)();
  void(*Update)();
};

// definition of the heat state machine : state & properties
typedef struct {
  heatState* currentState;
  float floorTemp;
  float inletTemp;
  float floorTarget;
  float hotWaterThreshold;
  uint32_t stateEnter;
  uint32_t stateEnterMinutes;
  RTCZero* rtc;
  bool valve;
  bool pump;

} heatSM;

void init(RTCZero* rtc, float temperature, void(*sendCallback)(bool, bool));

void heatSwitchSM(heatState& newState);   // Change the state in the machine
void heatUpdateSM(float floorTemp, float inletTemp);                         // Update the state machine (transition once, then update) etc.
uint32_t heatTimeInState();                  // Time elapsed in state
bool heatCurrentStateIs(heatState& state);

/********************************************************************/
void HeatingTransition();
void Heating();
void CoolingTransition();
void Cooling();
void CheckFloorTempWhileCooling();
void CheckFloorTempWhileCoolingTransition();
void FetchHotWater();
void WaitForHotWater();
void setValve(bool state);
void setPump(bool state);

void setFloorTemperature(float temperature);
#endif
