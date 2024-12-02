//==============================================================================
//  FILE INFORMATION
//==============================================================================
//
//  Source:        
//
//  Project:       
//
//  Author:        
//
//  Date:          
//
//  Revision:      1.0
//
//==============================================================================
//  FILE DESCRIPTION
//==============================================================================
//
//! \file
//! 
//! 
//
//==============================================================================
//  REVISION HISTORY
//==============================================================================
//  Revision: 1.0  
//      
//

#ifndef ETH_PHY_TYPE
#define ETH_PHY_TYPE  ETH_PHY_LAN8720
#define ETH_PHY_ADDR  1
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_PHY_POWER -1
#define ETH_CLK_MODE  ETH_CLOCK_GPIO17_OUT
#endif

//
//==============================================================================
//  INCLUDES
//==============================================================================
#include <ArduinoMqttClient.h>
#include <ETH.h>
#include <ArduinoQueue.h>
#include "ESP32_NOW.h"
#include "esp_now.h"
#include <esp_mac.h>  // For the MAC2STR and MACSTR macros
#include <vector>
#include <WiFi.h>

//==============================================================================
//	CONSTANTS, TYPEDEFS AND MACROS 
//==============================================================================


//==============================================================================
//	LOCAL DATA STRUCTURE DEFINITION
//==============================================================================
#define ESPNOW_WIFI_CHANNEL 1
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

//==============================================================================
//	GLOBAL DATA DECLARATIONS
//==============================================================================
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

//==============================================================================
//	LOCAL FUNCTION PROTOTYPES
//==============================================================================
void WiFiEvent(WiFiEvent_t event);
void PublishDataToMQTTBroker(char *topic, char *macID, char *data);
bool InsertElemInQueue(char* MQTTTopic, char* MacID, char *data, int data_len);
void InitESPNow();
void OnESPNowDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int data_len);
void ReconnectMQTTClient();

//==============================================================================
//	LOCAL AND GLOBAL FUNCTIONS IMPLEMENTATION
//==============================================================================

//==============================================================================
//
//  void loop()
//
//!  This function is the Main Activity thread
//
//==============================================================================
void loop() {
  /*
  ##############################################################
  ##############################################################
                Uncomment Test Code for Dummy MQTT Events
  ##############################################################
  ##############################################################
  */
  // static int DataCount = '0' + 0;
  // char Buffer[4];
  // char macStr[18] = "0C:B8:15:4A:E0:80";
  // memset(Buffer, 0, 4);
  // memcpy(Buffer, &DataCount, 4);
  // InsertElemInQueue((char*)MQTT_PUBLISH_TOPIC, macStr, (char*)Buffer, 4);
  // DataCount++;


  if (eth_connected) 
  {
    if (!mqttClient.connected()) 
    {
      ReconnectMQTTClient();
    }
    
    // call poll() regularly to allow the library to send MQTT keep alives which avoids being disconnected by the broker
    if (DataQueue.isEmpty ())
    {
      mqttClient.poll();
    }
    else
    {
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

//==============================================================================
//
//  void setup()
//
//!  This function handels all the initialization Activities
//
//==============================================================================
void setup()
{
  
  //Initialize serial and wait for port to open:
  Serial.begin(115200);
  while (!Serial); // wait for serial port to connect. Needed for native USB port only


  /*
  ##############################################################
  ##############################################################
                CONNECT TO NETWORK VIA ETHERNET 
  ##############################################################
  ##############################################################
  */

  Serial.println("Connecting to LAN");
  Serial.println();
  //Ethernet Initialization
  while (ETH.begin() != true)
  {
    // failed, retry
    Serial.print(".");
    delay(1000);    
  }
    //WiFiClient client;
  if (!ethClient.connect(MQTT_BROKER_PATH, MQTT_BROKER_PORT)) {
    Serial.println("connection failed");
    return;
  }
  Serial.println("You're connected to the network");
  Serial.println();


  /*
  ##############################################################
  ##############################################################
                CONFIGURE WIFI IN ESPNOW MODE
  ##############################################################
  ##############################################################
  */

  //Set device in AP mode to begin with
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  while (!WiFi.STA.started()) {
    delay(100);
  }

  // Init ESPNow with a fallback logic
  InitESPNow();
  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info.
  esp_now_register_recv_cb(OnESPNowDataRecv);

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

//==============================================================================
//
//  void WiFiEvent(WiFiEvent_t event)
//
//!  This function handels Ethernet Event Call Backs
//
//==============================================================================
void WiFiEvent(WiFiEvent_t event)
{
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

//==============================================================================
//
//  void PublishDataToMQTTBroker(char *topic, char *macID, char *data)
//
//!  This function handels MQTT data publishing to the MQTT broker
//
//==============================================================================
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

//==============================================================================
//
//  bool InsertElemInQueue(char* MQTTTopic, char* MacID, char *data, int data_len)
//
//!  This function insert an elem into publish Queue
//
//==============================================================================
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

//==============================================================================
//
//  void reconnect()
//
//!  This function reconnects MQTT client
//
//==============================================================================
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
  }
};

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

//==============================================================================
//
//  void InitESPNow() 
//
//!  This function Init ESP Now with fallback
//
//==============================================================================
void InitESPNow() 
{
  WiFi.disconnect();
  if (ESP_NOW.begin()) {
    Serial.println("ESPNow Init Success");
  } else {
    Serial.println("ESPNow Init Failed");
    Serial.println("***********************************************");    
    Serial.println("***********     REBOOTING       ***************");
    Serial.println("***********************************************");
    Serial.println();
    
    // Retry InitESPNow, add a counte and then restart?
    // InitESPNow();
    // or Simply Restart
    ESP.restart();
  }

  ESP_NOW.onNewPeer(register_new_master, NULL);
}

//==============================================================================
//
//  void OnESPNowDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) 
//
//!  This function manages callback when data is recv from Master
//
//==============================================================================
void OnESPNowDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int data_len) 
{
  char macStr[18];
  uint8_t *mac_addr = info->src_addr;
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print("Last Packet Recv from: "); Serial.println(macStr);
  Serial.print("Last Packet Recv Data: "); Serial.println(*data);
  Serial.println("");

  InsertElemInQueue((char*)MQTT_PUBLISH_TOPIC, macStr, (char*)data, data_len);
}
