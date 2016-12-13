#include "iotHubLib.h"

// to init iothublib use syntax like <sensors,actors>,
// where sensors specifies number of sensors being used on this node
// and actors specifies the number of actors being used on this node
// the two arguments in the () are the hostname of the server and the port iothub is available on
iotHubLib<2,0> iothub("linserver",3000); // note lack of http:// prefix, do not add one

void actor1_callback(int actor_state) {
  Serial.print("Recieved actor_state: ");
  Serial.println(actor_state);
}

void setup() {
  iothub.Start();

  // add actors
  iothub.RegisterActor(actor1_callback);
}

void loop() {
  iothub.Tick();
}
