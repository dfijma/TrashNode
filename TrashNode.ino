
#include <PowerNodeV11.h> // -- this is an olimex board.

#include <ACNode.h>
#include "MachineState.h"
#include <ButtonDebounce.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>

#include "acmerootcert.h"

#define MACHINE "trash"
#define LEDPIN_RED 5
#define LEDPIN_YELLOW 4
#define LEDPIN_GREEN 2

#define BUTTONPIN_RED 13
#define BUTTONPIN_YELLOW 14
#define BUTTONPIN_GREEN 15

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 0;

ACNode node = ACNode(MACHINE);

MachineState machinestate = MachineState();
enum { ACTIVE = MachineState::START_PRIVATE_STATES, DEACTIVATING };

ButtonDebounce buttonRed(BUTTONPIN_RED, 250);
ButtonDebounce buttonYellow(BUTTONPIN_YELLOW, 250);
ButtonDebounce buttonGreen(BUTTONPIN_GREEN, 250);

LED red(LEDPIN_RED);
LED yellow(LEDPIN_YELLOW);
LED green(LEDPIN_GREEN);

TelnetSerialStream telnetSerialStream = TelnetSerialStream();
WiFiClientSecure client;
 
DynamicJsonDocument doc(4096);
unsigned long lastUpdatedChores = 0;
time_t nextCollection = 0;
  
void fetchChores() {
  
  HTTPClient http;
  if (!http.begin(client, "https://makerspaceleiden.nl/crm/chores/api/v1/list/empty_trash" )) {
    Log.println("failed to load chores from server");
    return;
  }

  int httpStatus = http.GET();

  if (httpStatus != 200) {
    Log.printf("GET chores failed, error: %d\n", httpStatus);
    return;
  };

  DeserializationError error = deserializeJson(doc, http.getString());
  if (error) {
    Log.println("error parsing json");
    return;
  }

  time_t timestamp = 0;
  JsonArray chores = doc["chores"].as<JsonArray>();
  if (!chores) {
    Log.println("doc[\"chores\"] is not an array");
    return;
  }
  for (JsonVariant chore : chores) {
    JsonArray events = chore["events"].as<JsonArray>();
    if (!events) {
      Log.println("chore[\"events\"] is not an array");
      return;
    }
    for (JsonVariant event : events) {
       timestamp = event["when"]["timestamp"]; // unix epoch
       if (timestamp > 0) break; 
    }
    if (timestamp > 0) break;
  }
  if (timestamp == 0) {
    Log.println("did not find a timestamp for next collection in json");
    return;
  } 
  Log.printf("json says: next collection @%d\n", timestamp);
  nextCollection = timestamp;
}

time_t epoch() {
  time_t now = 0;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Log.println("failed to get time");
    return now;
  }
  time(&now);
  return now;
}

void onButtonPressed(int pin) {
   Log.printf("button pressed: %d\n", pin);
   if (machinestate.state() == MachineState::WAITINGFORCARD) {
      machinestate = ACTIVE;
   } else {
     Log.println("button pressed while not ready");
   }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  machinestate.defineState(ACTIVE, "Active", LED::LED_ERROR, 5 * 1000, DEACTIVATING);
  machinestate.defineState(DEACTIVATING, "Deactivating", LED::LED_ERROR, 1 * 1000, MachineState::WAITINGFORCARD);
  
  red.set(LED::LED_OFF);
  yellow.set(LED::LED_OFF);
  green.set(LED::LED_OFF);

  client.setCACert(rootCACertificate);

  machinestate.setOnChangeCallback(MachineState::ALL_STATES, [](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    Log.print("state changed: "); Log.println(current);
    if (current == ACTIVE) {
      red.set(LED::LED_SLOW);
      yellow.set(LED::LED_FLASH);
      green.set(LED::LED_FLASH);
    } else {
      red.set(LED::LED_OFF);
      yellow.set(LED::LED_OFF);
      green.set(LED::LED_OFF);
    }
  });
  
  node.onConnect([]() {
    Log.println("Connected");
    //init and get the time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    epoch();
    machinestate = MachineState::WAITINGFORCARD;
  });
  
  node.onDisconnect([]() {
    Log.println("Disconnected");
    machinestate = MachineState::NOCONN;
  });
  
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = MachineState::WAITINGFORCARD;
  });

  buttonRed.setCallback([](int state) {
    onButtonPressed(BUTTONPIN_RED);
  });

  buttonYellow.setCallback([](int state) {
    onButtonPressed(BUTTONPIN_YELLOW);
  });

  buttonGreen.setCallback([](int state) {
    onButtonPressed(BUTTONPIN_GREEN);
  });

  node.addHandler(&machinestate);

  auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);
  Log.addPrintStream(t);
  Debug.addPrintStream(t);

  node.begin(BOARD_OLIMEX); // OLIMEX


  Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

void loop() {
  node.loop();
  long now = millis();
  time_t epochNow = epoch();
  switch (machinestate.state()) {
    case MachineState::WAITINGFORCARD:
      if ((lastUpdatedChores == 0) || (now - lastUpdatedChores) > 1 * 60 * 60 * 1000) {
        Log.println("updating chores");
        lastUpdatedChores = now;
        fetchChores();
      }  
      // yeah! actual business logic :-)
      if (nextCollection == 0 || epochNow == 0) {
        red.set(LED::LED_OFF);
        yellow.set(LED::LED_FLASH); // panic!
        green.set(LED::LED_OFF);
      } else {
        if ((nextCollection - epochNow) < 15 * 60 * 60) {
          red.set(LED::LED_FLASH); // put it outside!
          yellow.set(LED::LED_OFF);
          green.set(LED::LED_OFF);
        } else {
          red.set(LED::LED_OFF); 
          yellow.set(LED::LED_OFF);
          green.set(LED::LED_FLASH); // no action required
        }
      }
      break;
    case ACTIVE:
      break;
    case DEACTIVATING:
      break;
    default:
      break;
   }

}
