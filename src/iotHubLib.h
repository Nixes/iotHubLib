#include "ArduinoJson.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>
#include <aWOT.h>

// both these values are currently unused
#define wifi_connection_time 2000 // how long it takes on average to reconnect to wifi
#define sensor_aquisition_time 2000 // how long it takes to retrieve the sensor values
#define max_node_name_length 100 // the maxiumum length of a nodes (sensor / actor) name


struct sensor {
  char id[25]; // ids are 24 alphanumeric keys long, the extra char is for the null character
  const char* name; // sensor name limited to 99 characters
  enum {is_int, is_float, is_bool, is_string} data_type;
};
struct actor {
  char id[25]; // ids are 24 alphanumeric keys long, the extra char is for the null character
  const char* name; // actor name limited to 99 characters
  // for good example of using these "tagged unions" go to: http://stackoverflow.com/questions/18577404/how-can-a-mixed-data-type-int-float-char-etc-be-stored-in-an-array
  enum {is_int, is_float, is_bool} state_type;
  union {
    int istate;
    double fstate;
    bool bstate;
  } state;
  // pointer to a function that is run when a new actor state is received, this will also vary by state_type
  union {
    void (*icallback)(int);
    void (*fcallback)(double);
    void (*bcallback)(bool);
  } on_update;
};

template<const uint number_sensor_ids,const uint number_actor_ids> class iotHubLib {
private:
  char* iothub_server; // the location of the server
  int iothub_port;

  WiFiServer server{80}; // the server that accepts requests from the hub, note the () initialisation syntax in not supported in class bodies
  WebApp app; // the class used by aWOT

  uint sleep_interval = 120000; // default of 120 seconds
  const int ids_eeprom_offset = 1; // memory location for ids start +1, skipping zero

  boolean first_boot_bit; // memory location for ids start +1, skipping zero

  sensor sensors[number_sensor_ids];
  actor actors[number_actor_ids];
  uint last_sensor_added_index;
  uint last_actor_added_index; // the index of the last actor added

  // takes in the request object whose body is desired, and a pointer to c_string to write the string
  void CStringReqBody (Request &req, char* c_string_body) {
    uint c_string_body_len = 0;

    int bytebuff;
    while ((bytebuff = req.read()) != -1 && c_string_body_len < 50) {
      c_string_body[c_string_body_len] = (char)bytebuff;
      c_string_body_len++;
      // and a proper null terminator to the end
      if (c_string_body_len > 0) {
        c_string_body[c_string_body_len] = (char)0;
      }
    }
    //Serial.print(" Len: "); Serial.print(c_string_body_len); Serial.println("FIN");
  }

  // This finds and updates the actor with an id that matches that passed in. It also runs the corresponding callback.
  void PostActorStateHandler(Request &req, Response &res, char* actor_id, uint actor_id_length) {
    if (actor_id_length != 24) {
      Serial.println("The passed in actor_id was not the standard 24 characters long");
      return;
    }
    actor* actor = FindActor(actor_id);
    if (actor == NULL) { // make sure the id exists before sending anything
      Serial.println("Was unable to find matching actor");
      return;
    }

    // update the actor state
    char c_string_body[50];
    CStringReqBody(req, c_string_body);
    Serial.print("Actor state JSON: "); Serial.println(c_string_body);
    StaticJsonBuffer<50> request_json_buff;
    JsonObject& request_json = request_json_buff.parseObject(c_string_body);

    if (!request_json.success()) {
      Serial.println("Failed to parse actor state JSON");
      return;
    }

    Serial.println("Running callback...");

    // run the callback with the new state
    switch (actor->state_type) {
      case actor::is_int:
      Serial.println("Run int callback");
      actor->state.istate = request_json["state"];
      actor->on_update.icallback(actor->state.istate);
      break;

      case actor::is_float:
      actor->state.fstate = request_json["state"];
      actor->on_update.fcallback(actor->state.fstate);
      break;

      case actor::is_bool:
      actor->state.bstate = request_json["state"];
      actor->on_update.bcallback(actor->state.bstate);
      break;
    }

    // send a response with the new state to confirm the action has completed
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& json_obj = jsonBuffer.createObject();

    // determine type of actor so we can figure out which tagged union var to send
    switch (actor->state_type) {
      case actor::is_int:
      json_obj["state"] = actor->state.istate;
      break;

      case actor::is_float:
      json_obj["state"] = actor->state.fstate;
      break;

      case actor::is_bool:
      json_obj["state"] = actor->state.bstate;
      break;
    }

    res.success("application/json");
    json_obj.printTo(res); // send straight to http output
  }

  void GetActorsHandler(Request &req, Response &res) {
    Serial.println("Sensor Listing Requested");

    Serial.print("Number actor ids: ");
    Serial.println(number_actor_ids);

    StaticJsonBuffer<600> jsonBuffer;
    JsonArray& json_array = jsonBuffer.createArray();

    for (uint i = 0; i < number_actor_ids; i++) {
      Serial.print("Actor ID:"); Serial.println(actors[i].id);
      JsonObject& json_obj = json_array.createNestedObject();
      json_obj["id"] = actors[i].id;
      json_obj["name"] = actors[i].name;

      switch (actors[i].state_type) {
        case actor::is_int:
        json_obj["state"] = actors[i].state.istate;
        break;

        case actor::is_float:
        json_obj["state"] = actors[i].state.fstate;
        break;

        case actor::is_bool:
        json_obj["state"] = actors[i].state.bstate;
        break;
      }
    }

    res.success("application/json");
    json_array.printTo(res);
  }

  void DebugRequest(Request &request) {
    Serial.print("Request Type: ");
    switch(request.method()){
      case Request::MethodType::GET:
        Serial.println("GET");
      break;
      case Request::MethodType::POST:
        Serial.println("GET");
      break;
    }
    Serial.print("Location: ");
    Serial.println(request.urlPath());
    /* This is disabled as it actually consumes the request body so it cannot be later routes
    Serial.print("Request Body: ");
    char c_string_body[50];
    CStringReqBody(request, c_string_body);
    Serial.println(c_string_body);
    */
  }

  void PrintStringFragment (char * string_fragment, uint string_fragment_length) {
    if (string_fragment == NULL) {
      Serial.println("String fragment pointer was null");
      return;
    }
    Serial.print("String Fragment: ");
    for (uint i =0; i < string_fragment_length; i++) {
      Serial.print(string_fragment[i]);
    }
    Serial.println();
  }

  actor* FindActor(char* actor_id) {
    Serial.println("Searching for matching actor");
    for (uint i = 0; i < number_actor_ids; i++) {
      // check if identical, TODO: restructure as a while loop
      bool non_match_found = false;
      uint id_digit;
      for (id_digit = 0; id_digit < 24; id_digit++) { // 24 is number of digits in id excluding null terminator
        Serial.print(actors[i].id[id_digit]); Serial.print(" : "); Serial.println(actor_id[id_digit]);
        if (actors[i].id[id_digit] != actor_id[id_digit]){
          non_match_found = true;
          break;
        };
      }
      Serial.print("Id_digit: "); Serial.println(id_digit);
      if (id_digit == 24 && !non_match_found) {
        return &actors[i];
      }
    }
    return NULL;
  }

  void GetActorHandler(Request &req, Response &res, char* actor_id, uint actor_id_length) {
    Serial.println("Single Actor listing requested");

    if (actor_id_length != 24) {
      Serial.println("The passed in actor_id was not the standard 24 characters long");
      return;
    }
    actor* actor = FindActor(actor_id);
    if (actor == NULL) {
      Serial.println("Was unable to find matching actor");
      return;
    } // make sure the id exists before sending anything

    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& json_obj = jsonBuffer.createObject();

    json_obj["id"] = actor->id;
    json_obj["name"] = actor->name;

    // determine type of actor so we can figure out which tagged union var to send
    switch (actor->state_type) {
      case actor::is_int:
      json_obj["state"] = actor->state.istate;
      break;

      case actor::is_float:
      json_obj["state"] = actor->state.fstate;
      break;

      case actor::is_bool:
      json_obj["state"] = actor->state.bstate;
      break;
    }

    res.success("application/json");
    json_obj.printTo(res); // send straight to http output
  }

  // takes in two parameters, a pointer to the full url string, the location of the colon
  void RouteParameter(char* full_string, uint colon_location, char** route_parameter_pointer, uint * route_parameter_len) {
    if (full_string == NULL) return;
    const byte string_len = strlen(full_string);
    // find the end of the dynamic route parameter
    uint param_end = colon_location;
    while (param_end < string_len && full_string[param_end] != '/') {
      param_end++;
    }
    *route_parameter_pointer = full_string + colon_location; // the new pointer location is full string pointer location offset by colon location
    *route_parameter_len =  param_end - colon_location;
  }


  bool MatchRoute(char* full_string, char* route, uint* colon_location) {
    byte string_len = strlen(full_string);
    byte route_len = strlen(route);
    uint i = 0;

    while (i < string_len && i < route_len){
      if (full_string[i] == route[i]) {
        i++;
      } else if (route[i] == ':') {
        *colon_location = i;
        Serial.println("Matched colon");
        return true;
      } else {
        return false;
      }
    }
    if (i == route_len) {
      Serial.println("Matched whole route string");
      return true;
    }
    Serial.println("Returning false");
    return false;
  }

  // if they are the same this returns true
  bool CStringCompare(char* string_1, char* string_2) {
    if (strcmp(string_1, string_2) == 0) {
      return true;
    }
    return false;
  }

  // based on process method provided by aWOT
  void ProcessRequests(Client *client, char *buff, int buff_len) {
    if (client != NULL) {
      Request request;
      Response response;

      response.init(client);
      request.init(client, buff, buff_len);
      request.processRequest();
      if (request.method() == Request::INVALID) {
        response.fail();
      } else {
        Request::HeaderNode* header_tail;
        request.processHeaders(header_tail);

        // while there are more requests, keep processing them
        if (request.next()){
          bool route_found = false; // this should be set to true by any route conditional

          DebugRequest(request);
          Request::MethodType method =  request.method();
          char* url_path =  request.urlPath();

          if (method == Request::MethodType::GET) {
            // GET routes
            // static routes
            if (CStringCompare(url_path, "actors")) {
              route_found = true;
              GetActorsHandler(request,response);
            }
            // dynamic routes
            uint colon_location = 0;
            if (MatchRoute(url_path,"actors/:something",&colon_location)) {
              route_found = true;
              Serial.println("Matched actors/:something");

              char* route_parameter;
              uint route_parameter_len;
              RouteParameter(url_path,colon_location, &route_parameter, &route_parameter_len);
              Serial.print("Route Param: ");
              PrintStringFragment(route_parameter, route_parameter_len);
              GetActorHandler(request,response,route_parameter,route_parameter_len);
            }
          }
          else if (method == Request::MethodType::POST) {
            // POST routes
            // dynamic routes
            uint colon_location = 0;
            if (MatchRoute(url_path,"actors/:something",&colon_location)) {
              route_found = true;
              Serial.println("Matched actors/:something ");

              char* route_parameter;
              uint route_parameter_len;
              RouteParameter(url_path,colon_location, &route_parameter, &route_parameter_len);
              Serial.print("Route Param: ");
              PrintStringFragment(route_parameter, route_parameter_len);
              PostActorStateHandler(request,response,route_parameter,route_parameter_len);
            }
          }

          // if no route is found, send a 404
          if (!route_found) {
            Serial.println("internal rest server 404ed");
            response.notFound();
          }
        }
        request.reset();
        response.reset();
      }
    }
  }

  void CheckConnections() {
    WiFiClient client = server.available();
    if (client.available()){
      char request[SERVER_DEFAULT_REQUEST_LENGTH];
      ProcessRequests(&client, request, SERVER_DEFAULT_REQUEST_LENGTH);
    }
  }

  bool CheckFirstBoot() {
    //Serial.print("BootByte: "); Serial.println(EEPROM.read(0));
    // check first byte is set to 128, this indicates this is not the first boot
    if ( 128 == EEPROM.read(0) ) {
      // Previous boot detected
      first_boot_bit = false;
      return false;
    } else {
      // No Previous boot detected
      first_boot_bit = true;
      return true;
    }
  }

  void UnsetFirstBoot() {
    if (first_boot_bit == true) {
      // check first byte is set to 128, this indicates this is not the first boot
      EEPROM.write(0,128);
      EEPROM.commit();
      first_boot_bit = false;
    } else {
      Serial.println("No change in unset boot bit");
    }
  }

  void SetFirstBoot() {
    if (first_boot_bit == false) {
      EEPROM.write(0,0);
      EEPROM.commit();
      first_boot_bit = true;
    } else {
      Serial.println("No change in set boot bit");
    }
  }

  void SetFirstBootRestart() {
    SetFirstBoot();
    ESP.restart();
  }

  void ShowEeprom() {
    // first byte reserved
    Serial.println();
    Serial.print("Read bytes: ");

    for(int addr = 0; addr < 48 + ids_eeprom_offset;) {
      Serial.print("[");
      Serial.print( (char)EEPROM.read(addr));
      Serial.print("] ");
      addr++;
    }
    Serial.println("//end");
  }

  // writes the passed in id to a location in memory, it assumes either last_actor_added_index or last_sensor_added_index will be incremented afterwards
  void ReadId(char * p_write) {
    uint offset = (last_actor_added_index * 24) + (last_sensor_added_index * 24) + ids_eeprom_offset;

    // read an entire 24 byte id
    for(uint j = 0; j < 24;j++) {
        p_write[j] = (char)EEPROM.read(j+offset);
    }
    p_write[24] = 0; // add null character when reading into struct
    Serial.print("Read from eeprom into sensor_ids: "); Serial.println(p_write);
  }

  void WriteId(char * p_read) {
    ShowEeprom();
    uint offset = (last_actor_added_index * 24) + (last_sensor_added_index * 24) + ids_eeprom_offset;

    // write an entire 24 byte id
    for(uint j = 0; j < 24;j++) {
        EEPROM.write(j+offset, p_read[j] );
    }
    EEPROM.commit();
    ShowEeprom();
  }

  // this should read sensor ID's from internal memory if available, else ask for new ids from the given server
  void LoadIds() {
    // first byte reserved
    int addr = ids_eeprom_offset;

    // read sensor ids
    for(int i = 0; i< number_sensor_ids;i++) {
      ReadId(sensors[i].id);
      last_sensor_added_index++;
    }

    EEPROM.commit();
    Serial.print("Read bytes: "); Serial.println(addr-ids_eeprom_offset);
  };

  void GetIdFromJson(String json_string, char (*sensor_id)[25]) {
    StaticJsonBuffer<100> jsonBuffer;
    JsonObject& json_object = jsonBuffer.parseObject(json_string);
    const char* id = json_object["id"];
    //strcpy (to,from)
    strcpy (*sensor_id,id);
  }

  bool CheckActorRegistered(char * actor_id) {
    HTTPClient http;

    String url = "/api/actors/";
    url.concat(actor_id);

    http.begin(iothub_server,iothub_port,url);
    int http_code = http.GET();
    http.end();

    // actor was found
    if (http_code == 200) {
      return true;
    }
    // if it wasn't then it does not exist
    return false;
  }

  void BaseRegisterActor(actor *actor_ptr,char * state_type) {
    Serial.println("Registering actor");
    HTTPClient http;

    // Make a HTTP post
    http.begin(iothub_server,iothub_port,"/api/actors");

    // prep the json object
    StaticJsonBuffer<max_node_name_length+10> jsonBuffer;
    JsonObject& json_obj = jsonBuffer.createObject();
    json_obj["name"] = actor_ptr->name;
    json_obj["state_type"] = state_type;

    http.addHeader("Content-Type","application/json"); // important! JSON conversion in nodejs requires this

    // add the json to a string
    String json_string;
    json_obj.printTo(json_string);// this is great except it seems to be adding quotation marks around what it is sending
    // then send the json
    int http_code = http.POST(json_string);

    // then print the response over Serial
    Serial.print("Response ID: ");
    GetIdFromJson(http.getString(),&actor_ptr->id);

    Serial.print(*actor_ptr->id); Serial.println("///end");
    //Serial.println( sensor_id );

    http.end();

    if (http_code == 200) {
      WriteId(actor_ptr->id);
    }
  }

  void BaseRegisterSensor(sensor *sensor_ptr, const char* data_type){
    Serial.println("Registering sensor");
    HTTPClient http;

    // Make a HTTP post
    http.begin(iothub_server,iothub_port,"/api/sensors");

    // prep the json object
    StaticJsonBuffer<max_node_name_length+10> jsonBuffer;
    JsonObject& json_obj = jsonBuffer.createObject();
    json_obj["name"] = sensor_ptr->name;
    json_obj["data_type"] = data_type;

    http.addHeader("Content-Type","application/json"); // important! JSON conversion in nodejs requires this

    // add the json to a string
    String json_string;
    json_obj.printTo(json_string);// this is great except it seems to be adding quotation marks around what it is sending
    // then send the json
    int http_code = http.POST(json_string);

    // then print the response over Serial
    Serial.print("Response ID: ");
    GetIdFromJson(http.getString(),&sensor_ptr->id);

    Serial.print(*sensor_ptr->id); Serial.println("///end");
    //Serial.println( sensor_id );

    http.end();

    if (http_code == 200) {
      WriteId(sensor_ptr->id);
    }
  }

public:
  // constructor
  iotHubLib(char* tmp_server, int tmp_port) {
    iothub_server = tmp_server;
    iothub_port = tmp_port;
    last_actor_added_index = 0;
    last_sensor_added_index = 0;
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

    if (number_actor_ids > 0) {
      server.begin();
      Serial.println("Internal Actor Server Started");
    }
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
      // make sure the sensor value is not something crazy
      if (!isnormal(sensor_value)) {
        Serial.println("Sensor was abnormal (infinity, NaN, zero or subnormal) no data sent.");
        return;
      };

      Serial.print("Sensor "); Serial.print(sensor_index); Serial.print(" value "); Serial.println(sensor_value);

      HTTPClient http;

      // generate the URL for sensor
      String url = "/api/sensors/";
      url.concat(sensors[sensor_index].id);
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
        Serial.println("Sensor 404'd restarting");
        // set first boot so that sensors can be registered on next boot
        // restart and re-register sensors
        SetFirstBootRestart();
      }

      http.end();
  };

  bool ActorValidation(const char* actor_name) {
    // do some validation
    // check actor name not too long
    if (strlen(actor_name) > max_node_name_length) {
      Serial.println("Actor being registered had a name length over that set by max_node_name_length");
      return true;
    }
    // check that we don't already have too many actors
    if (last_actor_added_index > number_actor_ids) {
      Serial.println("Actor being registered was more than the number specified in initialisation.");
      return true;
    }
    return false;
  }

  bool SensorValidation(const char* sensor_name) {
    if (strlen(sensor_name) > max_node_name_length) {
      Serial.println("Sensor being registered had a name length over that set by max_node_name_length");
      return true;
    }
    // check that we don't already have too many sensors
    if (last_sensor_added_index > number_sensor_ids) {
      Serial.println("Sensor being registered was more than the number specified in initialisation.");
      return true;
    }
    return false;
  }

  // this function tests to see if all the specified sensors and actors were registered, if so it unsets the first boot bit
  void CheckAllRegistered() {
    if (CheckFirstBoot()) {
      if (last_actor_added_index == number_actor_ids &&
      last_sensor_added_index == number_sensor_ids) {
        Serial.println("All sensors and actors registered, unsetting first boot bit.");
        UnsetFirstBoot();
      }
    }
  }

  void RegisterActor(const char* actor_name ,void (*function_pointer)(int)) {
    if (ActorValidation(actor_name)) return;
    Serial.println("Int actor being registered");
    actor new_actor;
    new_actor.name = actor_name;
    new_actor.state_type = actor::is_int;
    new_actor.on_update.icallback = function_pointer;
    if (CheckFirstBoot()) {
      Serial.println("First boot, registering");
      BaseRegisterActor(&new_actor,"number");
    } else {
      Serial.println("Not first boot, loading ids from eeprom");
      ReadId(new_actor.id);
      // if previously stored actor is not registered then set as first boot
      if (!CheckActorRegistered(new_actor.id)) {
        Serial.println("Actor loaded from memory has expired, reobtaining ids");
        SetFirstBootRestart();
      }
    }
    actors[last_actor_added_index] = new_actor;
    last_actor_added_index++;
    CheckAllRegistered();
  }
  void RegisterActor(const char* actor_name ,void (*function_pointer)(bool)) {
    if (ActorValidation(actor_name)) return;
    Serial.println("Bool actor being registered");
    actor new_actor;
    new_actor.name = actor_name;
    new_actor.state_type = actor::is_bool;
    new_actor.on_update.bcallback = function_pointer;
    if (CheckFirstBoot()) {
      Serial.println("First boot, registering");
      BaseRegisterActor(&new_actor,"boolean");
    } else {
      Serial.println("Not first boot, loading ids from eeprom");
      ReadId(new_actor.id);
      // if previously stored actor is not registered then set as first boot
      if (!CheckActorRegistered(new_actor.id)) {
        Serial.println("Actor loaded from memory has expired, reobtaining ids");
        SetFirstBootRestart();
      }
    }
    actors[last_actor_added_index] = new_actor;
    last_actor_added_index++;
    CheckAllRegistered();
  }

  void AddDummyActors(void (*function_pointer)(int)) {
    // add as many actors as we have space for
    for (uint i = 0; i < number_actor_ids; i++) {
      actors[i].name = "Dummy actor name";
      char id[25] = "54tr65e4rsr2d3t57q9t3a1h";
      strncpy(actors[i].id,id,25);

      actors[i].state_type = actor::is_int;
      actors[i].state.istate = 10;
      actors[i].on_update.icallback = function_pointer;
    }
  }


  void RegisterSensor(const char* sensor_name, const char* data_type) {
    if (SensorValidation(sensor_name)) return;
    sensor new_sensor;
    new_sensor.name = sensor_name;
    if (CheckFirstBoot()) {
      Serial.println("First boot, registering");
      BaseRegisterSensor(&new_sensor,data_type);
    } else {
      Serial.println("Not first boot, loading ids from eeprom");
      ReadId(new_sensor.id);
    }
    sensors[last_sensor_added_index] = new_sensor;
    last_sensor_added_index++; // increment last actor added
    CheckAllRegistered();
  }

  void Tick() {
    if (number_actor_ids > 0) {
      CheckConnections();
      delay(20);
    }
    // disable wifi while sleeping
    //WiFi.forceSleepBegin();
    else if (number_sensor_ids > 0) {
      delay(sleep_interval); // note that delay has built in calls to yeild() :)
    }

    //unsigned long time_wifi_starting = millis();
    // re-enble wifi after sleeping
    //WiFi.forceSleepWake();

    //Serial.print("Wifi status: "); Serial.println(WiFi.status());

    //unsigned long time_wifi_started = millis();
    //Serial.print("Time taken to reconnect to wifi: "); Serial.println( time_wifi_started - time_wifi_starting );
  }
};
