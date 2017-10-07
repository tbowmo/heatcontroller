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

// Port definitions
#define ONE_WIRE_BUS MYSX_A1  
#define HEAT_VALVE   MYSX_D3_INT
#define CIRC_PUMP    MYSX_D4_INT

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
float floor_target_temperature = 22;
bool  heating = false;
float lastTemperature[4];
bool  fetchHotWater = false;
bool  circulationPump = false;

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
      floor_target_temperature = message.getFloat();      
    }      
  }    
  if (message.sensor == FETCH_HOT_WATER) {
    if (!heating) {
      digitalWrite(LED_YELLOW, HIGH);
      digitalWrite(HEAT_VALVE, HIGH); // turn on the heat valve)
      fetchHotWater = message.getBool();
    }
  }
  if (message.sensor == PUMP_ON) {
    setCircPumpState(message.getBool());
  }
  if (message.sensor == HEAT_ON) {
    setHeatingState(message.getBool());
  }
}

void receiveTime(uint32_t controllerTime)
{
  Serial.print("Time value received: ");
  
  Serial.println(controllerTime);
  
  rtc.setEpoch(controllerTime);
}

void loop() {
  uint32_t currEpoch = rtc.getEpoch();
  if (currEpoch != lastEpoch) {
    lastEpoch = currEpoch;
  
    if ((currEpoch % 30) == 0) {
      thermostat();
    }
    if ((currEpoch % 10) == 0) {
      if (fetchHotWater) checkForHotWater();
    }
    reportTemperatures();

    if ((currEpoch % 15) == 0) {
      Serial.print("Unix time = ");
      Serial.println(rtc.getEpoch());
    }
  }
}

void reportTemperatures() {
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
    if (abs(lastTemperature[i] - temperature) > 0.5 && temperature != -127.00 && temperature != 85.00) {
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

void setCircPumpState(bool state) {
  if (state != circulationPump) {
    circulationPump = state;
    digitalWrite(CIRC_PUMP, state);
    digitalWrite(LED_BLUE, state);
    send(msgStatus.setSensor(PUMP_ON).set(state));
  }
}

void setHeatingState(bool state) {
  if (heating != state) {
    heating = state;
    digitalWrite(HEAT_VALVE, state); // Turn on heat valve
    digitalWrite(LED_RED, state);
    send(msgStatus.setSensor(HEAT_ON).set(state));
  }
}

void thermostat() {
  float tempC = sensors.getTempC(Sensors[temp_heatloop]);
  if (tempC < (floor_target_temperature - hysterisis)) {    
    setHeatingState(ON);
    setCircPumpState(ON);
  }
  if (tempC > (floor_target_temperature + hysterisis)) {
    setHeatingState(OFF);
    setCircPumpState(OFF);
  }
}

void checkForHotWater() {
  float tempC = sensors.getTempC(Sensors[temp_inlet]);
  if (tempC > 50.0) {
    digitalWrite(LED_YELLOW, LOW);
    if (!heating) digitalWrite(HEAT_VALVE, LOW);
    fetchHotWater = false;
    send(msgStatus.setSensor(FETCH_HOT_WATER).set(false));
  }
}


