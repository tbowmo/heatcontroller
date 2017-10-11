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
#include <OneWire.h>
#include <DallasTemperature.h>
#include <MySensors.h>
#include <RTCZero.h>
#include "heatState.h"

// Sensor child definitions

#define FLOOR_HEAT_LOOP 0
#define TEMP_INLET      1
#define TEMP_OUTLET     2
#define TEMP_HOT_WATER  3

#define FETCH_HOT_WATER 5
#define HEAT_ON         6
#define PUMP_ON         7


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

// Temperature sensors (Hardcoded here, as it makes it so much easier to reference them later on)
DeviceAddress Sensors[4] = {/*temp_heatloop = */{ 0x28, 0x56, 0xDB, 0x30, 0x0, 0x0, 0x0, 0xE4 }, // 1
                            /*temp_in =       */{ 0x28, 0x76, 0xAB, 0x19, 0x5, 0x0, 0x0, 0xE9 }, // 2
                            /*temp_out =      */{ 0x28, 0x81, 0xB9, 0x19, 0x5, 0x0, 0x0, 0xE1 }, // 3
                            /*temp_hot_water  */{ 0x28, 0xB1, 0x1F, 0x31, 0x0, 0x0, 0x0, 0xFA }};


const float hysterisis = 1.0;

// Mysensor Message objects
MyMessage msgTemperature(FLOOR_HEAT_LOOP, V_TEMP);
MyMessage msgStatus(HEAT_ON, V_STATUS);

// Global variables
float lastTemperature[4];
float currTemperature[4];

void saveFloorTemp(float temp) {
  uint16_t t = temp*10;
  Serial.print("Storing value ");
  Serial.println(t);
  
}

float fetchFloorTemp() {
  return 25.0;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println("HeatController 1.0");
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(HEAT_VALVE, OUTPUT);
  pinMode(CIRC_PUMP, OUTPUT);
  rtc.begin();
  float floorTemp = fetchFloorTemp();
  init(&rtc, floorTemp, &sendRelayStates);
  MyMessage test(FLOOR_HEAT_LOOP, V_HVAC_SETPOINT_HEAT);
  send(test.set(floorTemp,1));
}

void presentation() {
  sendSketchInfo("HeatController", "1.0");
  present(FLOOR_HEAT_LOOP, S_HEATER);
  present(TEMP_INLET, S_TEMP);
  present(TEMP_OUTLET, S_TEMP);
  present(TEMP_HOT_WATER, S_TEMP);
  present(FETCH_HOT_WATER, S_LIGHT);
  present(HEAT_ON, S_LIGHT);
  present(PUMP_ON, S_LIGHT);
  requestTime();
}

void receive(const MyMessage &message) {
  Serial.print(F("Remote command : "));
  if (message.sensor == FLOOR_HEAT_LOOP) {
    if (message.type == V_HVAC_SETPOINT_HEAT) {
      setFloorTemperature(message.getFloat());
      saveFloorTemp(message.getFloat());
    }
  }
  if (message.sensor == FETCH_HOT_WATER) {
    FetchHotWater();
  }
}

void receiveTime(uint32_t controllerTime)
{
  rtc.setEpoch(controllerTime);
}

void loop() {
  uint32_t currEpoch = rtc.getEpoch();
  if (currEpoch != lastEpoch) {
    lastEpoch = currEpoch;
    reportTemperatures(rtc.getMinutes() % 30 == 0);
    heatUpdateSM(currTemperature[0],currTemperature[1]);

    if ((currEpoch % 15) == 0) {
      Serial.print("Unix time = ");
      Serial.println(rtc.getEpoch());
    }
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

void sendRelayStates(bool valve, bool pump) {
  send(msgStatus.setSensor(HEAT_ON).set(valve));
  send(msgStatus.setSensor(PUMP_ON).set(pump));
}

