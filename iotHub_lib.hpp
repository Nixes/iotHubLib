#include "ArduinoJson.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>

// both these values are currently unused
#define wifi_connection_time 2000 // how long it takes on average to reconnect to wifi
#define sensor_aquisition_time 2000 // how long it takes to retrieve the sensor values

template<uint array_size> class iotHubLib {
private:
  char* iothub_server; // the location of the server
  int iothub_port = 80;


  uint sleep_interval = 30000; // default of 10 seconds
  const int sensor_ids_eeprom_offset = 1; // memory location for sensor ids start +1, skipping zero
  char sensor_ids[array_size][25]; // array of sensor ID's, sensor ids are 24 alphanumeric keys long, the extra char is for the null character

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
    EEPROM.commit();
  }

  void SetFirstBoot() {
    EEPROM.write(0,0);
    EEPROM.commit();
  }

  void ShowEeprom() {
    // first byte reserved
    Serial.println();
    Serial.print("Read bytes: ");

    for(int addr = 0; addr < 48 + sensor_ids_eeprom_offset;) {
      Serial.print("[");
      Serial.print( (char)EEPROM.read(addr));
      Serial.print("] ");
      addr++;
    }
    Serial.println("//end");
  }

  // this should read sensor ID's from internal memory if available, else ask for new ids from the given server
  void LoadSensors() {
    // first byte reserved
    int addr = sensor_ids_eeprom_offset;
    for(int i = 0; i< array_size;i++) {

      for(int j = 0; j < 24;j++) {
          sensor_ids[i][j] = (char)EEPROM.read(addr);
          addr++;
      }
      Serial.print("Read from eeprom into sensor_ids: "); Serial.println(sensor_ids[i]);

    }
    EEPROM.commit();
    Serial.print("Read bytes: "); Serial.println(addr-sensor_ids_eeprom_offset);
  };

  // this should save sensor ID's to internal memory
  void SaveSensors() {
    // first byte reserved
    int addr = sensor_ids_eeprom_offset;
    for(int i = 0; i< array_size;i++) {

      for(int j = 0; j < 24;j++) {
          EEPROM.write(addr, sensor_ids[i][j] );
          addr++;
      }

    }
    EEPROM.commit();
    Serial.print("Wrote bytes: "); Serial.println(addr-sensor_ids_eeprom_offset);
  };

  void GetIdFromJson(String json_string, char (*sensor_id)[25]) {
    StaticJsonBuffer<100> jsonBuffer;
    JsonObject& json_object = jsonBuffer.parseObject(json_string);
    const char* id = json_object["id"];
    //strcpy (to,from)
    strcpy (*sensor_id,id);
  }

  void RegisterSensor(const char* sensor_name,char (*sensor_id)[25]) {
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
    GetIdFromJson(http.getString(),*&sensor_id);

    Serial.print(*sensor_id); Serial.println("///end");
    //Serial.println( sensor_id );

    http.end();
  }

  // test that saved sensors Ids are still registered
  void CheckRegistered() {
    HTTPClient http;
    http.begin(iothub_server,iothub_port,"/api/sensors/");
    http.end();
  }

public:
  // constructor
  iotHubLib(char* server, int port) {
    iothub_server = server;
    iothub_port = port;
  };
  // destructor
  ~iotHubLib() {
  };

  void Start() {
    Serial.begin(115200);
    WiFi.begin(); // wifi configuration is outside the scope of this lib, use whatever was last used

    Serial.println("Establishing Wifi Connection");
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.print(".");
    }
    Serial.println();
    Serial.print("DONE - Got IP: "); Serial.println(WiFi.localIP());

    EEPROM.begin(512); // so we can read / write EEPROM

    Serial.print("Using Server: "); Serial.print(iothub_server); Serial.print(" Port: "); Serial.println(iothub_port);
  }

  void ClearEeprom() {
    // clear eeprom
    for (int i = 0 ; i < 256 ; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.commit();
  }

  void StartConfig() {};


  void Send(uint sensor_index,float sensor_value) {
      Serial.print("Sensor "); Serial.print(sensor_index); Serial.print(" value "); Serial.println(sensor_value);

      HTTPClient http;

      // generate the URL for sensor
      String url = "/api/sensors/";
      url.concat(sensor_ids[sensor_index]);
      url.concat("/data");
      Serial.print("Url: "); Serial.println(url);
      // Make a HTTP post
      http.begin(iothub_server,iothub_port, url);

      // prep the json object
      StaticJsonBuffer<50> jsonBuffer;
      JsonObject& json_obj = jsonBuffer.createObject();
      json_obj["value"] = sensor_value;

      http.addHeader("Content-Type", "application/json"); // important! JSON conversion in nodejs requires this

      // add the json to a string
      String json_string;
      json_obj.printTo(json_string);// this is great except it seems to be adding quotation marks around what it is sending
      // then send the json

      Serial.print("Sending Data: "); Serial.println(json_string);
      int http_code = http.POST(json_string);

      // then print the response over Serial
      Serial.print("Response: "); Serial.println( http.getString() );
      Serial.print("HTTP Code: "); Serial.println(http_code);Serial.println();

      if (http_code == 404) {
        // set first boot so that sensors can be registered on next boot
        SetFirstBoot();
        // restart and re-register sensors
        Serial.println("Sensor 404'd restarting");
        ESP.restart();
      }

      http.end();
  };

  // this should post to /api/sensors with the requested sensor, this will then return an ID that can be used
void RegisterSensors(const char* sensor_names[]) {
    ShowEeprom();
    // if first boot
    if ( CheckFirstBoot() ) {
      Serial.println("First boot, getting fresh sensor IDs from server");
      // ClearEeprom
      ClearEeprom();
      // register sensors
      for(uint i=0; i < array_size;i++ ) {
         RegisterSensor(sensor_names[i],&sensor_ids[i]);
      }
      // save sensor ids to eeprom
      SaveSensors();
      // change boot status
      UpdateFirstBoot();
    } else {
      // otherwise load from eeprom
      Serial.println("Not first boot, loading IDs from eeprom");
      LoadSensors();
    }
    ShowEeprom();
  };

  void Tick() {
    // disable wifi while sleeping
    //WiFi.forceSleepBegin();

    delay(sleep_interval); // note that delay has built in calls to yeild() :)

    //unsigned long time_wifi_starting = millis();
    // re-enble wifi after sleeping
    //WiFi.forceSleepWake();

    //Serial.print("Wifi status: "); Serial.println(WiFi.status());

    //unsigned long time_wifi_started = millis();
    //Serial.print("Time taken to reconnect to wifi: "); Serial.println( time_wifi_started - time_wifi_starting );
  }
};
