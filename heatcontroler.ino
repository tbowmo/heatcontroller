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

// Sensor child definitions

#define ON              true
#define OFF             false
// Object creation
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
RTCZero rtc;

const uint8_t temp_heatloop  = 0;
const uint8_t temp_inlet     = 1;
const uint8_t temp_outlet    = 2;
const uint8_t temp_hot_water = 3;
uint32_t lastEpoch;

const uint8_t sensor_loop_config = 10;

// Temperature sensors (Hardcoded here, as it makes it so much easier to reference them later on)
const DeviceAddress Sensors[4] = {/*temp_heatloop = */{ 0x28, 0x56, 0xDB, 0x30, 0x0, 0x0, 0x0, 0xE4 }, // 1
                            /*temp_in =       */{ 0x28, 0x76, 0xAB, 0x19, 0x5, 0x0, 0x0, 0xE9 }, // 2
                            /*temp_out =      */{ 0x28, 0x81, 0xB9, 0x19, 0x5, 0x0, 0x0, 0xE1 }, // 3
                            /*temp_hot_water  */{ 0x28, 0xB1, 0x1F, 0x31, 0x0, 0x0, 0x0, 0xFA }};

bool states[10];

const float hysterisis = 1.0;

// Mysensor Message objects
MyMessage msgTemperature(temp_heatloop, V_TEMP);
MyMessage msgStatus(VALVE_SWITCH, V_STATUS);

// Global variables
float lastTemperature[4];
float currTemperature[4];

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
  setFloorTemperature(floorTemp);
  setHotwaterThreshold(37);
  
  MyMessage test(temp_heatloop, V_HVAC_SETPOINT_HEAT);
  send(test.set(floorTemp,1));
  send(test.setSensor(temp_inlet).set(37.0,1));
  
}

void presentation() {
  sendSketchInfo("HeatController", "1.0");
  present(temp_heatloop, S_HEATER);
  present(temp_inlet, S_TEMP);
  present(temp_outlet, S_TEMP);
  present(temp_hot_water, S_TEMP);
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
    if(message.sensor == temp_heatloop) {
      setFloorTemperature(message.getFloat());
      saveFloorTemp(message.getFloat());
    }
    if (message.sensor == temp_inlet) {
      setHotwaterThreshold(message.getFloat());
    }
    MyMessage test(message.sensor, V_HVAC_SETPOINT_HEAT);
    send(test.set(message.getFloat(),1)); // Domoticz is only updating it's GUI with a value that is send from us, so send it back again.
  }
  if (message.sensor == FETCH_HOT_WATER) {
    Serial.println("fetching hot water");
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
    heatUpdateSM(currTemperature[0],currTemperature[1]);
  }
  
}

void reportTemperatures(bool force) {
  // Fetch temperatures from Dallas sensors
  sensors.requestTemperatures();

  // query conversion time and sleep until conversion completed
  int16_t conversionTime = sensors.millisToWaitForConversion(sensors.getResolution());
  wait(conversionTime);

  // Read temperatures and send them to controller
  for (int i=0; i< 4; i++) {
    // Fetch and round temperature to one decimal
    float temperature = sensors.getTempC(Sensors[i]);
    // Only send data if temperature has changed and no error
    if (temperature != -127.00 && temperature != 85.00) {
      currTemperature[i] = temperature;
      if (force || (abs(lastTemperature[i] - temperature) > 0.5)) {
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


