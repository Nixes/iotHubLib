#include "iotHubLib.h"

// to init iothublib use syntax like <sensors,actors>,
// where sensors specifies number of sensors being used on this node
// and actors specifies the number of actors being used on this node
// the two arguments in the () are the hostname of the server and the port iothub is available on
iotHubLib<1,0> iothub("linserver",3000); // note lack of http:// prefix, do not add one

void setup() {
  iothub.Start();
  iothub.RegisterSensor("Minimal Example Test Sensor","number"); // name and data_type of sensor
}

void loop() {
  iothub.Send(0,111); // sends value 111 to the sever between ticks
  iothub.Tick();
}
