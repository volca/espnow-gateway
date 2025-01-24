/*
    ESP-NOW Broadcast Slave
    Lucas Saavedra Vaz - 2024

    This sketch demonstrates how to receive broadcast messages from a master device using the ESP-NOW protocol.

    The master device will broadcast a message every 5 seconds to all devices within the network.

    The slave devices will receive the broadcasted messages. If they are not from a known master, they will be registered as a new master
    using a callback function.
*/

#ifndef ETH_PHY_TYPE
//#define ETH_PHY_TYPE  ETH_PHY_IP101
#define ETH_PHY_TYPE  ETH_PHY_LAN8720
#define ETH_PHY_ADDR  1
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_PHY_POWER -1
#define ETH_CLK_MODE  ETH_CLOCK_GPIO17_OUT
#endif

#include <Arduino.h>
#include "ESP32_NOW.h"
#include "WiFi.h"
#include "ETH.h"
#include <ArduinoQueue.h>
#include <ArduinoJson.h>
#include <ArduinoMqttClient.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
// For the MAC2STR and MACSTR macros
#include <esp_mac.h>  

#include <vector>
#include <map>
#include "bthome_base_common.h"
#include "bthome_parser.h"

/* Definitions */

#define MQTTTopicLength		128
#define MQTTDataLength		256
#define MQTTQueueMaxElem  10
#define MACIDSTRINGLENGTH 18

#define ESPNOW_WIFI_CHANNEL 1

#define PAYLOAD_PRESS       "PRESS"       

using namespace bthome_base;

struct QueueElem_t
{
    char MacID[MACIDSTRINGLENGTH];
	char Data[MQTTDataLength];
    size_t DataLen;
};

struct SensorInfo {
  char mac[MACIDSTRINGLENGTH];
  bool initialized;
  uint8_t type;
};

WiFiClient ethClient;
MqttClient mqttClient(ethClient);

bool eth_connected = false;
char currentSensorMac[MACIDSTRINGLENGTH];

std::map<String, uint8_t> sensors;

ArduinoQueue<QueueElem_t> DataQueue(MQTTQueueMaxElem);
// To be filled by the user, please visit: https://www.hivemq.com/demos/websocket-client/
const char MQTT_BROKER_PATH[] = "192.168.31.20";
int        MQTT_BROKER_PORT   = 1883;
const char MQTT_CLIENT_ID[] = "clientId-tPbkEIA7IS";

void printHex(uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (data[i] < 0x10) {
            Serial.print("0"); // 补零
        }
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}

bool InsertElemInQueue(char* MacID, char *data, int data_len)
{
  QueueElem_t QueueItem;
  memset(&QueueItem, 0x00, sizeof(QueueItem));

  Serial.println("Putting Element In Queue.......");
  Serial.println();
  memcpy(QueueItem.MacID, MacID, strlen(MacID));
  memcpy(QueueItem.Data, data, data_len);
  QueueItem.DataLen = data_len;
  
  DataQueue.enqueue(QueueItem);

  Serial.println("Successfully");
  Serial.print("MacID: "); Serial.println(QueueItem.MacID);
  Serial.println();

  return true;
}

/* Classes */

// Creating a new class that inherits from the ESP_NOW_Peer class is required.

class ESP_NOW_Peer_Class : public ESP_NOW_Peer {
public:
  // Constructor of the class
  ESP_NOW_Peer_Class(const uint8_t *mac_addr, uint8_t channel, wifi_interface_t iface, const uint8_t *lmk) : ESP_NOW_Peer(mac_addr, channel, iface, lmk) {}

  // Destructor of the class
  ~ESP_NOW_Peer_Class() {}

  // Function to register the master peer
  bool add_peer() {
    if (!add()) {
      log_e("Failed to register the broadcast peer");
      return false;
    }
    return true;
  }

  // Function to print the received messages from the master
  void onReceive(const uint8_t *data, size_t len, bool broadcast) {
    Serial.printf("Received a message from master " MACSTR " (%s)\n", MAC2STR(addr()), broadcast ? "broadcast" : "unicast");
    char mac[20];
    sprintf(mac, MACSTR, MAC2STR(addr()));
    // TODO
    InsertElemInQueue(mac, (char*)data, len);
  }
};

/* Global Variables */

// List of all the masters. It will be populated when a new master is registered
std::vector<ESP_NOW_Peer_Class> masters;

/* Callbacks */

// Callback called when an unknown peer sends a message
void register_new_master(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {
  if (memcmp(info->des_addr, ESP_NOW.BROADCAST_ADDR, 6) == 0) {
    Serial.printf("Unknown peer " MACSTR " sent a broadcast message\n", MAC2STR(info->src_addr));
    Serial.println("Registering the peer as a master");

    ESP_NOW_Peer_Class new_master(info->src_addr, ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, NULL);

    masters.push_back(new_master);
    if (!masters.back().add_peer()) {
      Serial.println("Failed to register the new master");
      return;
    }
  } else {
    // The slave will only receive broadcast messages
    log_v("Received a unicast message from " MACSTR, MAC2STR(info->src_addr));
    log_v("Igorning the message");
  }
}

void ReconnectMQTTClient() {
  Serial.println("Attempting MQTT ReConnection...");

  // Loop until we're reconnected
  while (!mqttClient.connected()) {  
    mqttClient.setId(MQTT_CLIENT_ID);

    if (!mqttClient.connect(MQTT_BROKER_PATH, MQTT_BROKER_PORT)) 
    {
      Serial.print("MQTT connection failed! Error code = ");
      Serial.println(mqttClient.connectError());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }

  Serial.println("You're connected to the MQTT broker!");
  Serial.println();
}

void startOta() {
    ArduinoOTA.setHostname("espnow-gw");
    Serial.print("Start OTA host - ");
    Serial.println(ArduinoOTA.getHostname());
    ArduinoOTA
        .onStart([]() {
          String type;
          if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
          } else {  // U_SPIFFS
            type = "filesystem";
          }

          // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
          Serial.println("Start updating " + type);
        })
        .onEnd([]() {
          Serial.println("\nEnd");
        })
        .onProgress([](unsigned int progress, unsigned int total) {
          Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        })
        .onError([](ota_error_t error) {
          Serial.printf("Error[%u]: ", error);
          if (error == OTA_AUTH_ERROR) {
            Serial.println("Auth Failed");
          } else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin Failed");
          } else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect Failed");
          } else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive Failed");
          } else if (error == OTA_END_ERROR) {
            Serial.println("End Failed");
          }
        });

  ArduinoOTA.begin();
}

void onEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname("ab-espnow-gw");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      startOta();
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}

/* Main */

void setup() {
  Serial.begin(115200);
  delay(1000);

  Network.onEvent(onEvent);
  while (ETH.begin() != true) {
    // failed, retry
    Serial.print(".");
    delay(1000);    
  }

  // Initialize the Wi-Fi module
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  while (!WiFi.STA.started()) {
    delay(100);
  }

  Serial.println("ESP-NOW Example - Broadcast Slave");
  Serial.println("Wi-Fi parameters:");
  Serial.println("  Mode: STA");
  Serial.println("  MAC Address: " + WiFi.macAddress());
  Serial.printf("  Channel: %d\n", ESPNOW_WIFI_CHANNEL);

  // Initialize the ESP-NOW protocol
  if (!ESP_NOW.begin()) {
    Serial.println("Failed to initialize ESP-NOW");
    Serial.println("Reeboting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  // Register the new peer callback
  ESP_NOW.onNewPeer(register_new_master, NULL);

  Serial.println("Setup complete. Waiting for a master to broadcast a message...");

  /*
  ##############################################################
  ##############################################################
                CONNECT TO MQTT BROKER
  ##############################################################
  ##############################################################
  */
  Serial.print("Attempting to connect to the MQTT broker: ");
  Serial.println(MQTT_BROKER_PATH);

  mqttClient.setId(MQTT_CLIENT_ID);

  if (!mqttClient.connect(MQTT_BROKER_PATH, MQTT_BROKER_PORT)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());
    //while (1);
  } else {
      Serial.println("You're connected to the MQTT broker!");
      Serial.println();

      // Allow the hardware to sort itself out
      delay(5000);
  }
}

void PublishDataToMQTTBroker(char *topic, char *data) {
    Serial.println("Sending Data To MQTT Broker");
    Serial.print("Topic: "); Serial.println(topic);
    Serial.println();

    //send message, the Print interface can be used to set the message contents
    mqttClient.beginMessage(topic);
    mqttClient.print(data);
    mqttClient.endMessage();
}

void handleMeasurement(uint8_t measurement_type, float value) {
    using namespace bthome_base;
    uint8_t *p = (uint8_t *)&value;
    Serial.printf("button evt %d\n", BTHOME_BUTTON_EVENT);
    switch (measurement_type)
    {
        case BTHOME_BUTTON_EVENT:
        case BTHOME_DIMMER_EVENT:
            {
                char topic[MQTTTopicLength];
                char message[MQTTDataLength];
                JsonDocument jsonDoc;
                String macString = String(currentSensorMac);
                macString.replace(":", "");
                bthome_measurement_event_record_t event_data{measurement_type, (uint8_t)((int)value & 0xff), (uint8_t)((int)value << 8 & 0xff)};
                bthome_measurement_record_t data{.is_value = false, .d = {.event = event_data}};
                Serial.printf("BTN/DIM Type %d value %f\n", measurement_type, value);
                if (sensors.find(currentSensorMac) == sensors.end()) {
                    sensors[currentSensorMac] = measurement_type;
                    sprintf(topic, "homeassistant/binary_sensor/%s/config", macString);
                    jsonDoc["device"]["identifiers"] = macString;
                    jsonDoc["state_topic"] = String("home/button/") + macString + String("/state");
                    jsonDoc["unique_id"] = String("button_") + macString;
                    jsonDoc["expire_after"] = 20;
                    serializeJson(jsonDoc, message);
                    PublishDataToMQTTBroker(topic, message);
                    Serial.println("Init sensor");
                }

                uint8_t type = sensors[currentSensorMac];
                jsonDoc.clear();
                sprintf(topic, "home/button/%s/state", macString);

                PublishDataToMQTTBroker(topic, (char *)"ON");
                break;
            }
        case BTHOME_PACKET_ID_VALUE:
            //packet_id = value; // intentional fallthrough
        default:
            {
                bthome_measurement_value_record_t value_data{measurement_type, value};
                bthome_measurement_record_t data{.is_value = true, .d = {.value = value_data}};
                Serial.printf("Type %d value %f\n", measurement_type, value);
            }
            break;
    }
}

void handleLog(const char *message) {
    Serial.print("parse log: ");
    Serial.println(message);
}

void loop() {
    if (!eth_connected) {
        delay(10);
        return;
    }
        
    if (!mqttClient.connected()) {
        Serial.println("Reconnect MQTT broker");
        ReconnectMQTTClient();
    }

    // call poll() regularly to allow the library to send MQTT keep alives which avoids being disconnected by the broker
    if (DataQueue.isEmpty ()) {
        mqttClient.poll();
    } else {
        while (!DataQueue.isEmpty ()) {
            QueueElem_t elem;
            elem = DataQueue.dequeue();
            strcpy(currentSensorMac, elem.MacID);
            bthome_base::parse_payload_bthome(
                (uint8_t *)&elem.Data[5],
                elem.DataLen - 5,
                bthome_base::BTProtoVersion_BTHomeV2,
                handleMeasurement, 
                handleLog
            );
        }
    }

    ArduinoOTA.handle();
    delay(10);
}
