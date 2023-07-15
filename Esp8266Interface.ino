#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

/****** WiFi Connection Details *******/
const char* ssid = "E3000";
const char* password = "121441006";
/******* MQTT Broker Connection Details *******/
const char* mqtt_server = "4a6e96ef33634394b57ba1cb380f4c0c.s2.eu.hivemq.cloud";
const char* mqtt_username = "jimharrigan";
const char* mqtt_password = "HiveMQ2021";
const int mqtt_port =8883;

/**** Secure WiFi Connectivity Initialisation *****/
WiFiClientSecure espClient;

/**** MQTT Client Initialisation Using WiFi Connection *****/
PubSubClient client(espClient);

unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];
/*
//************* Connect to WiFi ***********
void setup_wifi() {
    delay(10);
    Serial.print("\nConnecting to ");
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    randomSeed(micros());
    Serial.println("\nWiFi connected\nIP address: ");
    Serial.println(WiFi.localIP());
}
*/

/************* Connect to MQTT Broker ***********/
void reconnect() {
    // Loop until we're reconnected
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        String clientId = "ESP8266Client-";   // Create a random client ID
        clientId += String(random(0xffff), HEX);
        // Attempt to connect
        if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("connected");

            client.subscribe("led_state");   // subscribe the topics here

        }
        else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");   // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

/***** Call back Method for Receiving MQTT messages and Switching LED ****/

void callback(char* topic, byte* payload, unsigned int length) {
    String incommingMessage = "";
    for (int i = 0; i < length; i++) incommingMessage += (char)payload[i];

    Serial.println("Message arrived [" + String(topic) + "]" + incommingMessage);

    //--- check the incomming message
    if (strcmp(topic, "led_state") == 0) {
        //if (incommingMessage.equals("1")) digitalWrite(led, HIGH);   // Turn the LED on
        //else digitalWrite(led, LOW);  // Turn the LED off
    }

}

/**** Method for Publishing MQTT Messages **********/
void publishMessage(const char* topic, String payload, boolean retained) {
    if (client.publish(topic, payload.c_str(), true))
        Serial.println("Message publised [" + String(topic) + "]: " + payload);
}

/**** Application Initialisation Function******/
void setup() {

    Serial.begin(115200);
    while (!Serial) delay(1);

    //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wm;

    bool res;
    // res = wm.autoConnect(); // auto generated AP name from chipid
    // res = wm.autoConnect("AutoConnectAP"); // anonymous ap
    res = wm.autoConnect("AutoConnectAP", "password"); // password protected ap

    if (!res) {
        Serial.println("Failed to connect");
        // ESP.restart();
    }
    else {
        //if you get here you have connected to the WiFi    
        Serial.println("connected...yeey :)");
    }

    //setup_wifi();

    espClient.setInsecure();

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
}

/******** Main Function *************/
void loop() {

    if (!client.connected()) reconnect(); // check if client is connected
    client.loop();

    //read DHT11 temperature and humidity reading
    float humidity = 50;
    float temperature = 60;

    DynamicJsonDocument doc(1024);

    doc["deviceId"] = "NodeMCU";
    doc["siteId"] = "My Demo Lab";
    doc["humidity"] = humidity;
    doc["temperature"] = temperature;

    char mqtt_message[128];
    serializeJson(doc, mqtt_message);

    publishMessage("esp8266_data", mqtt_message, true);

    delay(50000);

}



