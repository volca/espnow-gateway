/*
    ESP-NOW Broadcast Slave
    Lucas Saavedra Vaz - 2024

    This sketch demonstrates how to receive broadcast messages from a master device using the ESP-NOW protocol.

    The master device will broadcast a message every 5 seconds to all devices within the network.

    The slave devices will receive the broadcasted messages. If they are not from a known master, they will be registered as a new master
    using a callback function.
*/

#ifndef ETH_PHY_TYPE
#define ETH_PHY_TYPE  ETH_PHY_LAN8720
#define ETH_PHY_ADDR  1
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_PHY_POWER -1
#define ETH_CLK_MODE  ETH_CLOCK_GPIO17_OUT
#endif

#include "ESP32_NOW.h"
#include "WiFi.h"
#include "ETH.h"
#include <ArduinoQueue.h>
#include <ArduinoMqttClient.h>

#include <esp_mac.h>  // For the MAC2STR and MACSTR macros

#include <vector>

#define MQTTTopicLength		128
#define MQTTDataLength		256
#define MQTTQueueMaxElem  10
#define MACIDSTRINGLENGTH 18

struct QueueElem_t
{
	char Topic[MQTTTopicLength];
  char MacID[MACIDSTRINGLENGTH];
	char Data[MQTTDataLength];
};

/* Definitions */

#define ESPNOW_WIFI_CHANNEL 1

WiFiClient ethClient;
MqttClient mqttClient(ethClient);

static bool eth_connected = false;
static bool isStartupComplete = false;

ArduinoQueue<QueueElem_t> DataQueue(MQTTQueueMaxElem);
// To be filled by the user, please visit: https://www.hivemq.com/demos/websocket-client/
const char MQTT_BROKER_PATH[] = "192.168.31.20";
int        MQTT_BROKER_PORT   = 1883;
const char MQTT_PUBLISH_TOPIC[]  = "WT32-ETH01/espNowEndpoint";
const char MQTT_CLIENT_ID[] = "clientId-tPbkEIA7IS";


bool InsertElemInQueue(char* MQTTTopic, char* MacID, char *data, int data_len)
{
  QueueElem_t QueueItem;
  memset(&QueueItem, 0x00, sizeof(QueueItem));

  Serial.println("Putting Element In Queue.......");
  Serial.println();
  memcpy(QueueItem.Topic, MQTTTopic, strlen(MQTTTopic));
  memcpy(QueueItem.MacID, MacID, strlen(MacID));
  memcpy(QueueItem.Data, data, data_len);
  
  DataQueue.enqueue(QueueItem);

  Serial.println("Successfully");
  Serial.print("Topic: "); Serial.println(QueueItem.Topic);
  Serial.print("MacID: "); Serial.println(QueueItem.MacID);
  Serial.print("Data: "); Serial.println(QueueItem.Data);
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
    Serial.printf("  Message: %s\n", (char *)data);
    InsertElemInQueue((char*)MQTT_PUBLISH_TOPIC, "<test-mac>", (char*)data, len);
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

void ReconnectMQTTClient() 
{
  Serial.print("Attempting MQTT ReConnection...");

  // Loop until we're reconnected
  while (!mqttClient.connected()) 
  {  
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


void onEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname("esp32-ethernet");
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
  while (!Serial) {
    delay(10);
  }

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

    while (1);
  }
  Serial.println("You're connected to the MQTT broker!");
  Serial.println();

  // Allow the hardware to sort itself out
  delay(5000);

  isStartupComplete = true;
}

void PublishDataToMQTTBroker(char *topic, char *macID, char *data)
{
    Serial.println("Sending Data To MQTT Broker");
    Serial.print("Topic: "); Serial.println(topic);
    Serial.print("MACID: "); Serial.println(macID);
    Serial.print("Data: ");  Serial.println(data);
    Serial.println();

    //send message, the Print interface can be used to set the message contents
    mqttClient.beginMessage(topic);
    mqttClient.print(macID); mqttClient.print(" ::Data:: "); mqttClient.print(data);
    mqttClient.endMessage();
}

void loop() {
    if (eth_connected) {
        if (!mqttClient.connected()) {
            Serial.println("Reconnect MQTT broker");
            ReconnectMQTTClient();
        }

        // call poll() regularly to allow the library to send MQTT keep alives which avoids being disconnected by the broker
        if (DataQueue.isEmpty ()) {
            mqttClient.poll();
        } else {
            while (!DataQueue.isEmpty ())
            {
                QueueElem_t elem;
                elem = DataQueue.dequeue();
                PublishDataToMQTTBroker(elem.Topic, elem.MacID, elem.Data);
            }
        }
    }

    delay(10000);
}
