#include "iotHub_lib.hpp"
#include "DHT.h" // using adafruit DHT lib: https://github.com/adafruit/DHT-sensor-library

// init iothublib, number in <> specifies number of sensors being used on this node
// the two arguments are the hostname of the server and the port iothub is available on
iotHubLib<2> iothub("linserver",3000); // note lack of http:// prefix, do not add one

// init the temp sensors
DHT dht_0(5, DHT22);
DHT dht_1(4, DHT22);

void setup() {
  iothub.Start();

  // add sensors
  const char *names[] = {"Temperature Sensor 1","Temperature Sensor 2"};
  iothub.RegisterSensors(names);
}

void loop() {
  iothub.Send(0, dht_0.readTemperature() );
  iothub.Send(1, dht_1.readTemperature() );
  iothub.Tick();
}
