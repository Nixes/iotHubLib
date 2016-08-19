#include "ArduinoJson.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>

template<uint array_size> class iotHubLib {
private:
  char* iothub_server; // the location of the server
  int iothub_port = 80;


  int tick_time = 10000; // default of 10 seconds
  char sensor_ids[array_size][25]; // array of sensor ID's, sensor ids are 25 alphanumeric keys long
  //char* sensor_ids[][25]; // array of sensor ID's, sensor ids are 25 alphanumeric keys long


  void ClearEeprom() {
    // clear eeprom
    for (int i = 0 ; i < 256 ; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.commit();
  }

  bool CheckFirstBoot() {
    Serial.print("BootByte: "); Serial.println(EEPROM.read(0));
    // check first byte is set to 128, this indicates this is not the first boot
    if ( 128 == EEPROM.read(0) ) {
      Serial.println("Previous boot detected, loading existing sensor configs");
      return false;
    } else {
      Serial.println("No Previous boot detected");
      return true;
    }
  }

  void UpdateFirstBoot() {
    // check first byte is set to 128, this indicates this is not the first boot
    EEPROM.write(0,128);
  }

  // this should read sensor ID's from internal memory if available, else ask for new ids from the given server
  void LoadSensors() {
    // first byte reserved
    int addr = 1;
    Serial.print("Read bytes: ");

    for(int i = 0; i< 256;i++) {
      Serial.print( (char)EEPROM.read(addr));
      addr++;
    }
    Serial.println(" //end");
  };

  // this should save sensor ID's to internal memory
  void SaveSensors() {
    // first byte reserved
    int addr = 1;
    for(int i = 0; i< array_size;i++) {

      for(int j = 0; j < 24;j++) {
          EEPROM.write(addr, sensor_ids[i][j] );
          addr++;
      }

    }
    EEPROM.commit();
    Serial.print("Wrote bytes: "); Serial.println(addr);
  };

  void GetIdFromJson(String json_string, char (*sensor_id)[25]) {
    StaticJsonBuffer<100> jsonBuffer;
    JsonObject& json_object = jsonBuffer.parseObject(json_string);
    const char* id = json_object["id"];
    //strcpy (to,from)
    strcpy (*sensor_id,"new0000000000000000000");
  }

  void RegisterSensor(char* sensor_name) {
    Serial.println("Registering sensor");
    HTTPClient http;

    // Make a HTTP post
    http.begin(iothub_server,iothub_port,"/api/sensors");

    // prep the json object
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& json_obj = jsonBuffer.createObject();
    json_obj["name"] = sensor_name;

    http.addHeader("Content-Type","application/json"); // important! JSON conversion in nodejs requires this

    // add the json to a string
    String json_string;
    json_obj.printTo(json_string);// this is great except it seems to be adding quotation marks around what it is sending
    // then send the json
    http.POST(json_string);

    // then print the response over Serial
    Serial.print("Response ID: ");
    char sensor_id[25] = "original0000000000000001";
    GetIdFromJson(http.getString(),&sensor_id);

    Serial.print(sensor_id); Serial.println("///end");
    //Serial.println( sensor_id );

    http.end();
  }

public:
  // constructor cannot contain parameters?
  iotHubLib(char* server, int port) {
    iothub_server = server;
    iothub_port = port;
    Serial.begin(115200);
    EEPROM.begin(512); // so we can read / write EEPROM
    LoadSensors();
  };
  // destructor
  ~iotHubLib() {
  };

  void StartConfig() {};


  void Send(uint sensor_index,float sensor_value) {
      HTTPClient http;

      // Make a HTTP post
      http.begin(iothub_server,iothub_port,"/api/sensors/" + sensor_ids[sensor_index] + "/data");

      // prep the json object
      StaticJsonBuffer<50> jsonBuffer;
      JsonObject& json_obj = jsonBuffer.createObject();
      json_obj["value"] = sensor_value;

      http.addHeader("Content-Type", "application/json"); // important! JSON conversion in nodejs requires this

      // add the json to a string
      String json_string;
      json_obj.printTo(json_string);// this is great except it seems to be adding quotation marks around what it is sending
      // then send the json
      http.POST(json_string);

      // then print the response over Serial
      Serial.print("Response: ");
      Serial.println( http.getString() );

      http.end();
  };

  // this should post to /api/sensors with the requested sensor, this will then return an ID that can be used
void RegisterSensors(char* sensor_names[]) {
    // if first boot
    if (CheckFirstBoot()) {
      // ClearEeprom
      ClearEeprom();
      // register sensors
      for(uint i=0; i < array_size;i++ ) {
         RegisterSensor(sensor_names[i]);
      }
      // change boot status
      UpdateFirstBoot();
    } else {
      // otherwise load from eeprom
      Serial.println("Sensor already loading from eeprom");
    }
  };
};
