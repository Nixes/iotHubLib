#include "iotHubLib.h"

// to init iothublib use syntax like <sensors,actors>,
// where sensors specifies number of sensors being used on this node
// and actors specifies the number of actors being used on this node
// the two arguments in the () are the hostname of the server and the port iothub is available on
iotHubLib<0,2> iothub("linserver",3000); // note lack of http:// prefix, do not add one

// these callbacks are automatically run when a response comes in from the server
void actor1_callback(int actor_state) {
  Serial.print("Recieved actor_state: ");
  Serial.println(actor_state);
}

// the type of callback tells the library what type of data the actor should accept
void actor2_callback(bool actor_state) {
  Serial.print("Recieved actor_state: ");
  Serial.println(actor_state);

  // toggle the built in led on the NodeMCU Board
  if (actor_state) {
    digitalWrite(2, LOW);
  } else {
    digitalWrite(2, HIGH);
  }
}

void setup() {
  pinMode(2,OUTPUT);

  iothub.Start();

  // add actors one at a time
  iothub.RegisterActor("Some real INTEGER actor hardware",actor1_callback);
  iothub.RegisterActor("Some real BOOLEAN actor hardware",actor2_callback);
}

void loop() {
  iothub.Tick();
}
