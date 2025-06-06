#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#define start_portal_pin 0
#define output_pin 1
#define control_pin 2
#define input_pin 3

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#else
#include <WiFi.h>
#endif

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>



ESP8266WiFiMulti WiFiMulti;

bool startPortalPending = 0;
bool shouldSaveConfig = false;
bool inputValue = 1;
bool lastInputValue = 1;  //assume not triggered on startup
char mqtt_server[64] = "";
int mqtt_port = 8883;
char mqtt_username[32] = "";
char mqtt_password[32] = "";
char mqtt_topic[32] = "";
char mqtt_control_topic[32] = "";
char trigger_url[64] = "";
char reset_url[64] = "";

WiFiManagerParameter custom_mqtt_server("server", "mqtt server", "", 63);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", "", 6);
WiFiManagerParameter custom_mqtt_user("user", "mqtt username", "", 31);
WiFiManagerParameter custom_mqtt_password("password", "mqtt password", "", 31);
WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", "", 31);
WiFiManagerParameter custom_mqtt_control_topic("control", "mqtt control topic", "", 31);
WiFiManagerParameter custom_trigger_url("triggerurl", "Trigger Url", "", 63);
WiFiManagerParameter custom_reset_url("reseturl", "Reset Url", "", 63);

/**** Secure WiFi Connectivity Initialisation *****/
WiFiClientSecure espClient;
WiFiManager wm;

// Checks if motion was detected, sets LED HIGH and starts a timer
ICACHE_RAM_ATTR void interruptHandler() {
  inputValue = digitalRead(input_pin);
}

/**** MQTT Client Initialisation Using WiFi Connection *****/
PubSubClient client(espClient);

/************* Connect to MQTT Broker ***********/
void reconnect() {
    if(!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        Serial.print("mqtt_username=");
        Serial.print(mqtt_username);
        Serial.print("|mqtt_password=");
        Serial.print(mqtt_password);
        Serial.print("|");
        String clientId = "ESP8266Client-";   // Create a random client ID
        clientId += String(random(0xffff), HEX);
        // Attempt to connect
        if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("connected");

            if(strlen(mqtt_control_topic) > 0)
            {
                Serial.print("Going to save->");
                Serial.print(mqtt_control_topic);
                client.subscribe(mqtt_control_topic);   // subscribe the topics here
                Serial.println("<-Done.");
            }

        }
        else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
        }
    }
}

void SaveConfig()
{
    //Serial.println("Saving config...");

    DynamicJsonDocument json(1024);

    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_username"] = mqtt_username;
    json["mqtt_password"] = mqtt_password;
    json["mqtt_topic"] = mqtt_topic;
    json["mqtt_control_topic"] = mqtt_control_topic;
    json["trigger_url"] = trigger_url;
    json["reset_url"] = reset_url;
    
    if (!LittleFS.begin()) {
        //Serial.println("LittleFS Mount Failed");
        return;
    }
    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
        //Serial.println("failed to open config file for writing");
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
    LittleFS.end();
    //end save

}

void LoadConfig()
{
    //Serial.println("Loading Config...");

    ////Serial.println(custom_mqtt_server.getValue());

    //read configuration from FS json
    //Serial.println("mounting FS...");

    if (LittleFS.begin()) {
        //Serial.println("mounted file system");
        if (LittleFS.exists("/config.json")) {
            //file exists, reading and loading
            //Serial.println("reading config file");
            File configFile = LittleFS.open("/config.json", "r");
            if (configFile) {
                //Serial.println("opened config file");
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);

                DynamicJsonDocument json(1024);
                auto deserializeError = deserializeJson(json, buf.get());
                serializeJson(json, Serial);
                if (!deserializeError) {
                    //Serial.println("\nparsed json");
                    //Serial.println(buf.get());

                    strcpy(mqtt_server, json["mqtt_server"]);
                    //Serial.println(mqtt_server);

                    mqtt_port = json["mqtt_port"];
                    //Serial.println(mqtt_port);

                    strcpy(mqtt_username, json["mqtt_username"]);
                    //Serial.println(mqtt_username);

                    strcpy(mqtt_password, json["mqtt_password"]);
                    //Serial.println(mqtt_password);

                    strcpy(mqtt_topic, json["mqtt_topic"]);
                    //Serial.println(mqtt_topic);

                    strcpy(mqtt_control_topic, json["mqtt_control_topic"]);
                    //Serial.println(mqtt_control_topic);

                    strcpy(trigger_url, json["trigger_url"]);
                    //Serial.println(trigger_url);

                    strcpy(reset_url, json["reset_url"]);
                    //Serial.println(reset_url);
                }
                else
                {
                    //Serial.println("failed to load json config");
                }
                configFile.close();
            }
        }
        LittleFS.end();
    }
    else {
        //Serial.println("failed to mount FS");

        //Try to format so we can save later
        if(LittleFS.format())
        {
          //Serial.println("Format complete");
        }
        else
        {
          //Serial.println("Format failed");
        }
    }
    //end read
}

void saveParamCallback() {
    //Serial.println("saveParamCallback()");
    shouldSaveConfig = true;
}

/***** Call back Method for Receiving MQTT messages and Switching LED ****/

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  
    String incommingMessage = "";
    for (int i = 0; i < length; i++) incommingMessage += (char)payload[i];

    Serial.println("Message arrived [" + String(topic) + "]" + incommingMessage);
    incommingMessage.toLowerCase();
    //--- check the incomming message
    if (String(topic).equals(mqtt_control_topic)) {
        if (incommingMessage.equals("1") || incommingMessage.equals("trigger") )
        {
            //Serial.println("TRIGGER CONTROL!!!");
            digitalWrite(output_pin, 1);
            publishMessage(mqtt_topic, "Output 1", true);
        }
        else
        {
          //Serial.println("RESET control");
          digitalWrite(output_pin, 0);
          publishMessage(mqtt_topic, "Output 0", true);
        }
    }

}

bool MakeRequest(String url)
{
    String result = "";

    if (WiFiMulti.run() == WL_CONNECTED) { //Check the current connection status
        ////Serial.println("WifiMulti.run() is connected");
        WiFiClient wiFiClient;
        HTTPClient http;

        http.begin(wiFiClient, url);
        ////Serial.print("Requesting:");
        ////Serial.println(url);
        int httpCode = http.GET();  //Make the request

        if (httpCode > 0) { //Check for the returning code

            result = http.getString();
            ////Serial.print("result is:");
            ////Serial.println(result);
        }

        http.end(); //Free the resources
    }

    if (result.indexOf("OK") > 0)
    {
        return true;
    }
    return false;
}

/**** Method for Publishing MQTT Messages **********/
void publishMessage(const char* topic, String payload, boolean retained) {
    if (client.publish(topic, payload.c_str(), true))
    {
        Serial.println("Message publised [" + String(topic) + "]: " + payload);
    }
    else
    {
      Serial.println("Failed to publish MQTT message.");
    }
}

/**** Application Initialisation Function******/
void setup() {

    //Set GPIO2 to output low so it can pull GPIO0 low with a switch only after boot.
    pinMode(control_pin, OUTPUT);
    digitalWrite(control_pin, 0);

    pinMode(start_portal_pin, INPUT);
    pinMode(input_pin, INPUT_PULLUP);

    Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
    while (!Serial) delay(1);
    Serial.println("Starting up!");

    LoadConfig();

    custom_mqtt_server.setValue(mqtt_server, 63);
    char mqtt_port_str[6] = "0";
    itoa(mqtt_port, mqtt_port_str, 10);
    custom_mqtt_port.setValue(mqtt_port_str, 6);
    custom_mqtt_user.setValue(mqtt_username, 31);
    custom_mqtt_password.setValue(mqtt_password, 31);
    custom_mqtt_topic.setValue(mqtt_topic, 31);
    custom_mqtt_control_topic.setValue(mqtt_control_topic, 31);
    custom_trigger_url.setValue(trigger_url, 63);
    custom_reset_url.setValue(reset_url, 63);

    // add all your parameters here
    wm.addParameter(&custom_mqtt_server);
    wm.addParameter(&custom_mqtt_port);
    wm.addParameter(&custom_mqtt_user);
    wm.addParameter(&custom_mqtt_password);
    wm.addParameter(&custom_mqtt_topic);
    wm.addParameter(&custom_mqtt_control_topic);
    wm.addParameter(&custom_trigger_url);
    wm.addParameter(&custom_reset_url);

    // set custom html head content , inside <head>
    // examples of favicon, or meta tags etc
    const char* headhtml = "<meta http-equiv=\"Cache-Control\" content=\"no-cache, no-store, must-revalidate\" /><meta http-equiv=\"Pragma\" content=\"no-cache\" /><meta http-equiv=\"Expires\" content=\"0\" />";
    wm.setCustomHeadElement(headhtml);

//  set custom html menu content , inside menu item "custom", see setMenu()
//  const char* menuhtml = "<form action='/custom' method='get'><button>Custom</button></form><br/>\n";
//  wm.setCustomMenuHTML(menuhtml);

    std::vector<const char *> menu = {"wifi","info","param","sep","erase","update","exit","restart"};
    wm.setMenu(menu); // custom menu, pass vector

    wm.setSaveParamsCallback(saveParamCallback);
    wm.setDarkMode(true);
    wm.setConnectTimeout(20); // how long to try to connect to WiFi for before continuing
    wm.setConfigPortalTimeout(300);
    wm.setConfigPortalBlocking(false);

    bool res;
    //res = wm.autoConnect("ESP", "1234567890"); // password protected ap

    if (!res) {
        //Serial.println("Failed to connect WiFi");
        // ESP.restart();
    }
    else {
        //if you get here you have connected to the WiFi    
        //Serial.println("WiFi connected.");
    }

    espClient.setInsecure();

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(mqttCallback);

    //Serial.end();
    //pinMode(output_pin, OUTPUT);
    attachInterrupt(digitalPinToInterrupt(input_pin), interruptHandler, CHANGE);
}

/******** Main Function *************/
void loop() {

    wm.process(); // avoid delays() in loop when non-blocking and other long running code

    if (!digitalRead(start_portal_pin))
    {
        if (!wm.startConfigPortal("ESP", "1234567890")) {
            //Serial.println("failed to connect and hit timeout");
        }
        else
        {
            //Serial.println("Portal Started");
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
        strcpy(mqtt_control_topic, custom_mqtt_control_topic.getValue());
        strcpy(trigger_url, custom_trigger_url.getValue());
        strcpy(reset_url, custom_reset_url.getValue());

        SaveConfig();
        shouldSaveConfig = false;
    }

    if(strlen(mqtt_server) > 0 )
    {
        if (!client.connected()) reconnect(); // check if MQTT client is connected
        client.loop();
    }

    //Serial.print(inputValue);
    //Serial.println(inputValue ? " Reset" : " Trigger");
    if(inputValue != lastInputValue)
    {
        lastInputValue = inputValue;

        if(client.connected())
        {
            publishMessage(mqtt_topic, inputValue ? "Reset" : "Trigger", true);
        }

        if(!inputValue && strlen(trigger_url) > 0 )
        {
            //Serial.println("Requesting trigger_url");
            //Serial.println(trigger_url);
            MakeRequest(trigger_url);
        }

        if(inputValue && strlen(reset_url) > 0 )
        {
          //Serial.println("Requesting reset_url");
            MakeRequest(reset_url);
        }

    }
}



