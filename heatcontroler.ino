/********************************************************
 *
 * Central heating thermostat and monitoring software
 *
 * 1. Measures 4 different temperatures in central heating system:
 *    a. Temperature of floor heating loop
 *    b. Temperature of hot water from district heating
 *    c. Temperature of returned water to district heating
 *    d. Temperature of hot tap water
 *
 *  2. Allow to set a setpoint for floor heating loop
 *    a. Control the floor heating, by opening / closing walve to
 *       let in hot water from district heating
 *    b. Control heating circulation pump. If heat is turned off (summer time)
 *       It's not necessary to power the circulation pump.
 *
 *  3. Function to "retrieve" heated water from district heating
 *     in case heating is turned off, we have to "draw" the heated water from
 *     the central pipes in the road, to the building, specially when prepsaring
 *     baths.
 *
 *     This function turns on the valve, and let hot water in, until input water
 *     exceeds 50 degrees celcius. Pump is not necessarily on.
 */


/*  This file contains the main initialization, and mysensors communications
 *  For the state machine, look at heatState.cpp / heatState.h 
 *  
 *  The state machine handles the thermostat activity, and run more or less
 *  autonomously.
 */

#define MY_RADIO_NRF24
#define MY_DEBUG
#define MY_NODE_ID 20
#define MY_REPEATER_FEATURE
#include <OneWire.h>
#include <DallasTemperature.h>
#include <MySensors.h>
#include <RTCZero.h>
#include "heatState.h"

#define FEEDBACK_INTERVAL 1800

// Object creation
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
RTCZero rtc;

// Mysensor Message objects
MyMessage msgTemperature(TEMP_HEATLOOP, V_TEMP);
MyMessage msgStatus(VALVE_SWITCH, V_STATUS);

// Temperature sensors (Hardcoded here, as it makes it so much easier to reference them later on)
const DeviceAddress Sensors[4] = {/*TEMP_HEATLOOP = */{ 0x28, 0x56, 0xDB, 0x30, 0x0, 0x0, 0x0, 0xE4 }, // 1
                            /*temp_in =       */{ 0x28, 0x76, 0xAB, 0x19, 0x5, 0x0, 0x0, 0xE9 }, // 2
                            /*temp_out =      */{ 0x28, 0x81, 0xB9, 0x19, 0x5, 0x0, 0x0, 0xE1 }, // 3
                            /*TEMP_HOT_WATER  */{ 0x28, 0xB1, 0x1F, 0x31, 0x0, 0x0, 0x0, 0xFA }};

// Global variables
float lastTemperature[4];
bool  states[10];
uint32_t lastEpoch;

void saveFloorTemp(float temp) {
  Serial.print("Storing value ");
  uint16_t t = temp*10;
  saveState(0, t & 0xff);
  Serial.print(t & 0xff);
  Serial.print(" - ");
  t = t >> 8;
  saveState(1, t & 0xff);
  Serial.println(t & 0xff);
}

float fetchFloorTemp() {
  uint16_t t = (loadState(1) << 8) | loadState(0);
  Serial.print("Loading data ");
  Serial.println(t / 10.0);
  return t / 10.0;
}

void setup() {
  Serial.begin(115200);
  //while (!Serial) {}
  Serial.println("HeatController 1.0");
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(HEAT_VALVE, OUTPUT);
  pinMode(CIRC_PUMP, OUTPUT);
  rtc.begin();
  float floorTemp = fetchFloorTemp();
  
  init(&rtc, sendRelayStates);
  setFloorThreshold(floorTemp);
  setHotwaterThreshold(37);
  
  MyMessage test(TEMP_HEATLOOP, V_HVAC_SETPOINT_HEAT);
  send(test.set(floorTemp,1));
  send(test.setSensor(TEMP_INLET).set(37.0,1));
}

void presentation() {
  sendSketchInfo("HeatController", "1.0");
  present(TEMP_HEATLOOP, S_HEATER);
  present(TEMP_INLET, S_TEMP);
  present(TEMP_OUTLET, S_TEMP);
  present(TEMP_HOT_WATER, S_TEMP);
  present(FETCH_HOT_WATER, S_LIGHT);
  present(VALVE_SWITCH, S_LIGHT);
  present(PUMP_SWITCH, S_LIGHT);
  present(SUMMER, S_LIGHT);
  requestTime();
}

void receive(const MyMessage &message) {
  Serial.print(F("Remote command : "));
  Serial.print(message.sensor);
  if (message.type == V_HVAC_SETPOINT_HEAT) {
    if(message.sensor == TEMP_HEATLOOP) {
      setFloorThreshold(message.getFloat());
      saveFloorTemp(message.getFloat());
    }
    if (message.sensor == TEMP_INLET) {
      setHotwaterThreshold(message.getFloat());
    }
    MyMessage test(message.sensor, V_HVAC_SETPOINT_HEAT);
    send(test.set(message.getFloat(),1)); // Domoticz is only updating it's GUI with a value that is send from us, so send it back again.
  }
  if (message.sensor == FETCH_HOT_WATER) {
    fetchHotWater();
  }
  if (message.sensor == SUMMER) {
    summer(message.getBool());
  }
  if (message.sensor == VALVE_SWITCH) {
    setValve(message.getBool());
  }
  if (message.sensor == PUMP_SWITCH) {
    setPump(message.getBool());
  }
}

void receiveTime(uint32_t controllerTime)
{
  rtc.setEpoch(controllerTime);
}

void loop() {
  uint32_t currEpoch = rtc.getEpoch();
  bool force = false;
  if (currEpoch != lastEpoch) {
    lastEpoch = currEpoch;
    force = (rtc.getEpoch() % FEEDBACK_INTERVAL) == 0;
    reportTemperatures(force);
    reportStates(force);
    heatUpdateSM();
  }
}

void reportTemperatures(bool force) {
  // Fetch temperatures from Dallas sensors
  sensors.requestTemperatures();

  // query conversion time and sleep until conversion completed
  int16_t conversionTime = sensors.millisToWaitForConversion(sensors.getResolution());
  wait(conversionTime);

  // Read temperatures and send them to controller
  for (uint8_t i=0; i< 4; i++) {
    // Fetch and round temperature to one decimal
    float temperature = sensors.getTempC(Sensors[i]);
    // Only send data if temperature has changed and no error
    if (temperature != -127.00 && temperature != 85.00) {
      currentTemperature(i, temperature); 
      if (force || (abs(lastTemperature[i] - temperature) > 0.2)) {
        // Send in the new temperature
        send(msgTemperature.setSensor(i).set(temperature,1));
        Serial.print(i);
        Serial.print(" : ");
        Serial.println(temperature);
        // Save new temperatures for next compare
        lastTemperature[i]=temperature;
      }
    }
  }
}

void reportStates(bool force) {
  if (force) {
    Serial.print(rtc.getEpoch());
    Serial.println(" -> Sending switch states");
    for (int i = 5; i < 9; i++) {
      send(msgStatus.setSensor(i).set(states[i]));
    }
  }
}

void sendRelayStates(uint8_t sensor, bool state) {
  Serial.print("Callback ");
  Serial.print(sensor);
  Serial.print(" - ");
  Serial.println(state);
  states[sensor] = state;
  send(msgStatus.setSensor(sensor).set(state));
}

