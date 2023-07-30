#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#define start_portal_pin 0
#define input_pin 3

#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

bool startPortalPending = 0;
bool shouldSaveConfig = false;
char mqtt_server[64] = "";
int mqtt_port = 8883;
char mqtt_username[32] = "";
char mqtt_password[40] = "";
char mqtt_topic[40] = "";

WiFiManagerParameter custom_mqtt_server("server", "mqtt server", "", 63);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", "", 6);
WiFiManagerParameter custom_mqtt_user("user", "mqtt username", "", 31);
WiFiManagerParameter custom_mqtt_password("password", "mqtt password", "", 39);
WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", "", 39);

/**** Secure WiFi Connectivity Initialisation *****/
WiFiClientSecure espClient;
WiFiManager wm;

/**** MQTT Client Initialisation Using WiFi Connection *****/
PubSubClient client(espClient);

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

            client.subscribe("start_portal");   // subscribe the topics here

        }
        else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");   // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void SaveConfig()
{
    Serial.println("Saving config...");

    DynamicJsonDocument json(1024);

    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_username"] = mqtt_username;
    json["mqtt_password"] = mqtt_password;
    json["mqtt_topic"] = mqtt_topic;

    if (!LittleFS.begin()) {
        Serial.println("LittleFS Mount Failed");
        return;
    }
    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
        Serial.println("failed to open config file for writing");
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
    LittleFS.end();
    //end save

}

void LoadConfig()
{
    Serial.println("Loading Config...");

    //Serial.println(custom_mqtt_server.getValue());

    //read configuration from FS json
    Serial.println("mounting FS...");

    if (LittleFS.begin()) {
        Serial.println("mounted file system");
        if (LittleFS.exists("/config.json")) {
            //file exists, reading and loading
            Serial.println("reading config file");
            File configFile = LittleFS.open("/config.json", "r");
            if (configFile) {
                Serial.println("opened config file");
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);

                DynamicJsonDocument json(1024);
                auto deserializeError = deserializeJson(json, buf.get());
                serializeJson(json, Serial);
                if (!deserializeError) {
                    Serial.println("\nparsed json");
                    Serial.println(buf.get());

                    strcpy(mqtt_server, json["mqtt_server"]);
                    Serial.println(mqtt_server);

                    mqtt_port = json["mqtt_port"];
                    Serial.println(mqtt_port);

                    strcpy(mqtt_username, json["mqtt_username"]);
                    Serial.println(mqtt_username);

                    strcpy(mqtt_password, json["mqtt_password"]);
                    Serial.println(mqtt_password);

                    strcpy(mqtt_topic, json["mqtt_topic"]);
                    Serial.println(mqtt_topic);
                }
                else {
                    Serial.println("failed to load json config");
                }
                configFile.close();
            }
        }
        LittleFS.end();
    }
    else {
        Serial.println("failed to mount FS");

        //Try to format so we can save later
        if(LittleFS.format())
        {
          Serial.println("Format complete");
        }
        else
        {
          Serial.println("Format failed");
        }
    }
    //end read
}

void saveParamCallback() {
    Serial.println("saveParamCallback()");
    shouldSaveConfig = true;
}

/***** Call back Method for Receiving MQTT messages and Switching LED ****/

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String incommingMessage = "";
    for (int i = 0; i < length; i++) incommingMessage += (char)payload[i];

    Serial.println("Message arrived [" + String(topic) + "]" + incommingMessage);

    //--- check the incomming message
    if (strcmp(topic, " ") == 0) {
        //if (incommingMessage.equals("1"))
        {
            startPortalPending = true;
        }
    }

}

/**** Method for Publishing MQTT Messages **********/
void publishMessage(const char* topic, String payload, boolean retained) {
    if (client.publish(topic, payload.c_str(), true))
        Serial.println("Message publised [" + String(topic) + "]: " + payload);
}

/**** Application Initialisation Function******/
void setup() {

    pinMode(start_portal_pin, INPUT);
    pinMode(input_pin, INPUT);

    Serial.begin(115200);
    while (!Serial) delay(1);
    Serial.println("Starting up!");

    LoadConfig();

    custom_mqtt_server.setValue(mqtt_server, 63);
    char mqtt_port_str[6] = "0";
    itoa(mqtt_port, mqtt_port_str, 10);
    custom_mqtt_port.setValue(mqtt_port_str, 6);
    custom_mqtt_user.setValue(mqtt_username, 31);
    custom_mqtt_password.setValue(mqtt_password, 39);
    custom_mqtt_topic.setValue(mqtt_topic, 39);

    // add all your parameters here
    wm.addParameter(&custom_mqtt_server);
    wm.addParameter(&custom_mqtt_port);
    wm.addParameter(&custom_mqtt_user);
    wm.addParameter(&custom_mqtt_password);
    wm.addParameter(&custom_mqtt_topic);

  // set custom html head content , inside <head>
  // examples of favicon, or meta tags etc
  // const char* headhtml = "<link rel='icon' type='image/png' href='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAYAAABXAvmHAAADQElEQVRoQ+2YjW0VQQyE7Q6gAkgFkAogFUAqgFQAVACpAKiAUAFQAaECQgWECggVGH1PPrRvn3dv9/YkFOksoUhhfzwz9ngvKrc89JbnLxuA/63gpsCmwCADWwkNEji8fVNgotDM7osI/x777x5l9F6JyB8R4eeVql4P0y8yNsjM7KGIPBORp558T04A+CwiH1UVUItiUQmZ2XMReSEiAFgjAPBeVS96D+sCYGaUx4cFbLfmhSpnqnrZuqEJgJnd8cQplVLciAgX//Cf0ToIeOB9wpmloLQAwpnVmAXgdf6pwjpJIz+XNoeZQQZlODV9vhc1Tuf6owrAk/8qIhFbJH7eI3eEzsvydQEICqBEkZwiALfF70HyHPpqScPV5HFjeFu476SkRA0AzOfy4hYwstj2ZkDgaphE7m6XqnoS7Q0BOPs/sw0kDROzjdXcCMFCNwzIy0EcRcOvBACfh4k0wgOmBX4xjfmk4DKTS31hgNWIKBCI8gdzogTgjYjQWFMw+o9LzJoZ63GUmjWm2wGDc7EvDDOj/1IVMIyD9SUAL0WEhpriRlXv5je5S+U1i2N88zdPuoVkeB+ls4SyxCoP3kVm9jsjpEsBLoOBNC5U9SwpGdakFkviuFP1keblATkTENTYcxkzgxTKOI3jyDxqLkQT87pMA++H3XvJBYtsNbBN6vuXq5S737WqHkW1VgMQNXJ0RshMqbbT33sJ5kpHWymzcJjNTeJIymJZtSQd9NHQHS1vodoFoTMkfbJzpRnLzB2vi6BZAJxWaCr+62BC+jzAxVJb3dmmiLzLwZhZNPE5e880Suo2AZgB8e8idxherqUPnT3brBDTlPxO3Z66rVwIwySXugdNd+5ejhqp/+NmgIwGX3Py3QBmlEi54KlwmjkOytQ+iJrLJj23S4GkOeecg8G091no737qvRRdzE+HLALQoMTBbJgBsCj5RSWUlUVJiZ4SOljb05eLFWgoJ5oY6yTyJp62D39jDANoKKcSocPJD5dQYzlFAFZJflUArgTPZKZwLXAnHmerfJquUkKZEgyzqOb5TuDt1P3nwxobqwPocZA11m4A1mBx5IxNgRH21ti7KbAGiyNn3HoF/gJ0w05A8xclpwAAAABJRU5ErkJggg==' />";
  // const char* headhtml = "<meta name='color-scheme' content='dark light'><style></style><script></script>";
  // wm.setCustomHeadElement(headhtml);

    // set custom html menu content , inside menu item "custom", see setMenu()
  //  const char* menuhtml = "<form action='/custom' method='get'><button>Custom</button></form><br/>\n";
  //  wm.setCustomMenuHTML(menuhtml);

  //  std::vector<const char *> menu = {"wifi","wifinoscan","info","param","custom","close","sep","erase","update","restart","exit"};
  //  wm.setMenu(menu); // custom menu, pass vector


    wm.setSaveParamsCallback(saveParamCallback);
    wm.setDarkMode(true);
    wm.setConfigPortalTimeout(120);

    bool res;
    res = wm.autoConnect("ESP", "1234567890"); // password protected ap

    if (!res) {
        Serial.println("Failed to connect");
        // ESP.restart();
    }
    else {
        //if you get here you have connected to the WiFi    
        Serial.println("connected...yeey :)");
    }

    espClient.setInsecure();

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(mqttCallback);
}

/******** Main Function *************/
void loop() {

    Serial.println("Loop");

    if (!digitalRead(start_portal_pin))
    {
        if (!wm.startConfigPortal("ESP", "1234567890")) {
            Serial.println("failed to connect and hit timeout");
        }
        else
        {
            Serial.println("Portal Started");
            startPortalPending = false;
        }
    }

    if (shouldSaveConfig)
    {
        strcpy(mqtt_server, custom_mqtt_server.getValue());
        mqtt_port = atoi(custom_mqtt_port.getValue());
        Serial.println(mqtt_port);
        strcpy(mqtt_username, custom_mqtt_user.getValue());
        strcpy(mqtt_password, custom_mqtt_password.getValue());
        strcpy(mqtt_topic, custom_mqtt_topic.getValue());

        SaveConfig();
        shouldSaveConfig = false;
    }

    if (!client.connected()) reconnect(); // check if client is connected
    client.loop();

    float humidity = 99;
    float temperature = 60;

    DynamicJsonDocument doc(1024);

    doc["deviceId"] = "NodeMCU";
    doc["siteId"] = "My Demo Lab";
    doc["humidity"] = 0;
    doc["temperature"] = digitalRead(input_pin);

    char mqtt_message[128];
    serializeJson(doc, mqtt_message);

    publishMessage(mqtt_topic, mqtt_message, true);

    delay(5000);

}



