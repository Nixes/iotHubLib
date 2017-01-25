#include "iotHubLib.h"
#include "DHT.h" // using adafruit DHT lib: https://github.com/adafruit/DHT-sensor-library

// to init iothublib use syntax like <sensors,actors>,
// where sensors specifies number of sensors being used on this node
// and actors specifies the number of actors being used on this node
// the two arguments in the () are the hostname of the server and the port iothub is available on
iotHubLib<2,0> iothub("linserver",3000); // note lack of http:// prefix, do not add one

// init the temp sensors
DHT dht_0(5, DHT22);
DHT dht_1(4, DHT22);

void setup() {
  iothub.Start();

  // add sensors
  iothub.RegisterSensor("Temperature Sensor 1","number");
  iothub.RegisterSensor("Temperature Sensor 2","number");
}

void loop() {
  iothub.Send(0, dht_0.readTemperature() );
  iothub.Send(1, dht_1.readTemperature() );
  iothub.Tick();
}
