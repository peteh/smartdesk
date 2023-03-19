#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <MqttDevice.h>
#include <esplog.h>
#include <LittleFS.h>

#include "utils.h"
#include "config.h"
#include "desk.h"

#define INPUT_UP 8
#define INPUT_DOWN 9

#define OUTPUT_UP 10
#define OUTPUT_DOWN 11

#define SENSOR_TRIGGER 12
#define SENSOR_ECHO 13

// when the position is in this range the control will stop
#define TARGET_ACCURACY_CM 5

bool g_inputUp = false;
bool g_inputDown = false;
Desk g_desk(OUTPUT_UP, OUTPUT_DOWN);

double g_targetHeightCm = 80.;
double g_sensorHeightCm = 60.;
bool g_control = false;

struct Config
{
  uint16_t preset1;
  uint16_t preset2;
  uint16_t preset3;
};

Config g_config = {0, 0, 0};

WiFiClient net;
PubSubClient client(net);

const char *HOMEASSISTANT_STATUS_TOPIC = "homeassistant/status";
const char *HOMEASSISTANT_STATUS_TOPIC_ALT = "ha/status";

MqttDevice mqttDevice(composeClientID().c_str(), "Smart Desk", "Smart Desk Control OMT", "maker_pt");
MqttText mqttHeight(&mqttDevice, "height", "Desk Height");
MqttSelect mqttPreset(&mqttDevice, "preset", "Desk Preset");
MqttText mqttConfigPreset1(&mqttDevice, "preset1", "Desk Preset 1");
MqttText mqttConfigPreset2(&mqttDevice, "preset2", "Desk Preset 2");
MqttText mqttConfigPreset3(&mqttDevice, "preset3", "Desk Preset 3");

#define CONFIG_FILENAME "/config.txt"

bool formatLittleFS()
{
  log_warn("need to format LittleFS: ");
  LittleFS.end();
  LittleFS.begin();
  log_info("Success: %d", LittleFS.format());
  return LittleFS.begin();
}

double readSensor()
{
  // Clears the trigPin
  digitalWrite(SENSOR_TRIGGER, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(SENSOR_TRIGGER, HIGH);
  delayMicroseconds(10);
  digitalWrite(SENSOR_TRIGGER, LOW);
  // Reads the echoPin, returns the sound wave travel time in microseconds
  long duration = pulseIn(SENSOR_ECHO, HIGH, 1 * 1000 * 1000);
  // Calculating the distance
  double distance = duration * 0.034 / 2.;
  // TODO: remove fake value
  return 60.;
  return distance;
}

void setNewTarget(double newTargetCm)
{
  log_info("New target height: %.2f", newTargetCm);
  g_targetHeightCm = newTargetCm;
  g_control = true;
}

void loadSettings()
{
  // Open file for reading
  // TODO: check if file exists
  File file = LittleFS.open(CONFIG_FILENAME, "r");

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<1024> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error)
  {
    log_error(F("Failed to read file, using default configuration"));
  }

  // Copy values from the JsonDocument to the Config
  g_config.preset1 = doc["preset1"] | g_config.preset1;
  g_config.preset2 = doc["preset2"] | g_config.preset2;
  g_config.preset3 = doc["preset3"] | g_config.preset3;

  // Close the file (Curiously, File's destructor doesn't close the file)
  file.close();
}


void saveSettings()
{
    // Open file for writing
    File file = LittleFS.open(CONFIG_FILENAME, "w");
    if (!file)
    {
        log_error("Failed to create config file");
        return;
    }
    // Allocate a temporary JsonDocument
    // Don't forget to change the capacity to match your requirements.
    // Use arduinojson.org/assistant to compute the capacity.
    StaticJsonDocument<1024> doc;

    // Set the values in the document
    doc["preset1"] = g_config.preset1;
    doc["preset2"] = g_config.preset2;
    doc["preset3"] = g_config.preset3;

    // Serialize JSON to file
    if (serializeJson(doc, file) == 0)
    {
        log_error("Failed to write config to file");
    }

    // Close the file
    file.close();
}

void publishMqttState(MqttEntity *device, const char *state)
{
  client.publish(device->getStateTopic(), state);
}

void publishMqttState(MqttEntity *device, const int16_t state)
{
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%d", state);
  publishMqttState(device, buffer);
}

void publishConfig(MqttEntity *device)
{
  String payload = device->getHomeAssistantConfigPayload();
  char topic[255];
  device->getHomeAssistantConfigTopic(topic, sizeof(topic));
  client.publish(topic, payload.c_str());

  device->getHomeAssistantConfigTopicAlt(topic, sizeof(topic));
  client.publish(topic,
                 payload.c_str());
}

void publishConfig()
{
  publishConfig(&mqttHeight);
  publishConfig(&mqttPreset);
  publishConfig(&mqttConfigPreset1);
  publishConfig(&mqttConfigPreset2);
  publishConfig(&mqttConfigPreset3);

  delay(1000);

  // TODO state of preset

  // publish all initial states
  publishMqttState(&mqttHeight, (uint16_t)g_sensorHeightCm);
  publishMqttState(&mqttConfigPreset1, g_config.preset1);
  publishMqttState(&mqttConfigPreset2, g_config.preset2);
  publishMqttState(&mqttConfigPreset3, g_config.preset3);
}

uint16_t parseValue(const char *data, unsigned int length)
{
  // TODO length check
  char temp[32];
  strncpy(temp, data, length);
  return (uint16_t)strtoul(temp, NULL, 10);
}

void callback(char *topic, byte *payload, unsigned int length)
{
  log_info("Message arrived [%s]", topic);
  for (unsigned int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp(topic, mqttHeight.getCommandTopic()) == 0)
  {
    uint16_t data = parseValue((char *)payload, length);
    setNewTarget(data);
  }
  if (strcmp(topic, mqttPreset.getCommandTopic()) == 0)
  {
    if (strncmp((char *)payload, "None", length) == 0)
    {
      log_info("Received None Preset - do nothing");
    }
    else if(strncmp((char *)payload, "1", length) == 0)
    {
      log_info("Received Preset 1");
      setNewTarget((double)g_config.preset1);
    }
    else if(strncmp((char *)payload, "2", length) == 0)
    {
      log_info("Received Preset 2");
      setNewTarget((double)g_config.preset2);
    }
    else if(strncmp((char *)payload, "3", length) == 0)
    {
      log_info("Received Preset 3");
      setNewTarget((double)g_config.preset3);
    }
    //uint16_t data = parseValue((char *)payload, length);
    //setNewTarget(data);
  }
  else if (strcmp(topic, mqttConfigPreset1.getCommandTopic()) == 0)
  {
    uint16_t data = parseValue((char *)payload, length);
    g_config.preset1 = data;
    log_info("Setting Preset 1 to '%d'", g_config.preset1);
    saveSettings();
    publishMqttState(&mqttConfigPreset1, g_config.preset1);
  }
  else if (strcmp(topic, mqttConfigPreset2.getCommandTopic()) == 0)
  {
    uint16_t data = parseValue((char *)payload, length);
    g_config.preset2 = data;
    log_info("Setting Preset 2 to '%d'", g_config.preset2);
    saveSettings();
    publishMqttState(&mqttConfigPreset2, g_config.preset2);
  }
  else if (strcmp(topic, mqttConfigPreset3.getCommandTopic()) == 0)
  {
    uint16_t data = parseValue((char *)payload, length);
    g_config.preset3 = data;
    log_info("Setting Preset 3 to '%d'", g_config.preset3);
    saveSettings();
    publishMqttState(&mqttConfigPreset3, g_config.preset3);
  }

  // publish config when homeassistant comes online and needs the configuration again
  else if (strcmp(topic, HOMEASSISTANT_STATUS_TOPIC) == 0 ||
           strcmp(topic, HOMEASSISTANT_STATUS_TOPIC_ALT) == 0)
  {
    if (strncmp((char *)payload, "online", length) == 0)
    {
      publishConfig();
    }
  }
}

void connectToMqtt()
{
  log_info("Connecting to MQTT...");
  // TODO: add security settings back to mqtt
  // while (!client.connect(mqtt_client, mqtt_user, mqtt_pass))
  while (!client.connect(composeClientID().c_str()))
  {
    log_debug(".");
    delay(4000);
  }

  client.subscribe(mqttHeight.getCommandTopic(), 1);
  client.subscribe(mqttPreset.getCommandTopic(), 1);
  client.subscribe(mqttConfigPreset1.getCommandTopic(), 1);
  client.subscribe(mqttConfigPreset2.getCommandTopic(), 1);
  client.subscribe(mqttConfigPreset3.getCommandTopic(), 1);

  client.subscribe(HOMEASSISTANT_STATUS_TOPIC);
  client.subscribe(HOMEASSISTANT_STATUS_TOPIC_ALT);

  // TODO: solve this somehow with auto discovery lib
  // client.publish(mqttTopic(MQTT_TOPIC_NONE, MQTT_ACTION_NONE).c_str(), "online");
  publishConfig();
}

void connectToWifi()
{
  log_info("Connecting to wifi...");
  // TODO: really forever? What if we want to go back to autoconnect?
  while (WiFi.status() != WL_CONNECTED)
  {
    log_debug(".");
    delay(1000);
  }
  log_info("Wifi connected!");
}

void setup()
{
  mqttHeight.setPattern("[0-9]+");
  mqttHeight.setMaxLetters(3);
  mqttHeight.setIcon("mdi:desk");

  mqttPreset.addOption("None");
  mqttPreset.addOption("1");
  mqttPreset.addOption("2");
  mqttPreset.addOption("3");

  mqttConfigPreset1.setPattern("[0-9]+");
  mqttConfigPreset1.setMaxLetters(3);
  mqttConfigPreset1.setIcon("mdi:desk");
  mqttConfigPreset1.setEntityType(EntityCategory::CONFIG);

  mqttConfigPreset2.setPattern("[0-9]+");
  mqttConfigPreset2.setMaxLetters(3);
  mqttConfigPreset2.setIcon("mdi:desk");
  mqttConfigPreset2.setEntityType(EntityCategory::CONFIG);

  mqttConfigPreset3.setPattern("[0-9]+");
  mqttConfigPreset3.setMaxLetters(3);
  mqttConfigPreset3.setIcon("mdi:desk");
  mqttConfigPreset3.setEntityType(EntityCategory::CONFIG);

  Serial.begin(115200);

  pinMode(SENSOR_TRIGGER, OUTPUT);
  pinMode(SENSOR_ECHO, INPUT);

  pinMode(INPUT_UP, INPUT_PULLDOWN);
  pinMode(INPUT_DOWN, INPUT_PULLDOWN);

  if (!LittleFS.begin())
  {
    log_error("Failed to mount file system");
    delay(5000);
    if (!formatLittleFS())
    {
      log_error("Failed to format file system - hardware issues!");
      for (;;)
      {
        delay(100);
      }
    }
  }
  loadSettings();

  g_desk.begin();

  WiFi.mode(WIFI_STA);
  WiFi.hostname(composeClientID().c_str());
  WiFi.begin(wifi_ssid, wifi_pass);

  connectToWifi();
  client.setBufferSize(512);
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  log_info("Connected to SSID: %s", wifi_ssid);
  log_info("IP address: %s", WiFi.localIP().toString().c_str());

  setupOTA();
  connectToWifi();
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    connectToWifi();
  }
  if (!client.connected())
  {
    connectToMqtt();
  }
  client.loop();
  ArduinoOTA.handle();

  // logic part
  bool inputUp = digitalRead(INPUT_UP);
  if (inputUp != g_inputUp)
  {
    log_info("Up pressed: %d", inputUp);
    g_inputUp = inputUp;
    // stop Controlling the table
    g_control = false;
    if (g_inputUp)
    {
      g_desk.moveUp();
    }
    else
    {
      g_desk.stop();
    }
  }
  bool inputDown = digitalRead(INPUT_DOWN);
  if (inputUp != g_inputUp)
  {
    log_info("Down pressed: %d", inputDown);
    g_inputDown = inputDown;
    // stop Controlling the table
    g_control = false;
    if (g_inputDown)
    {
      g_desk.moveDown();
    }
    else
    {
      g_desk.stop();
    }
  }

  if (g_control)
  {
    // TODO: read sensor value every x seconds and stop movement if not moving anymore
    double sensorCm = readSensor();
    if (g_desk.controlLoop(sensorCm, g_targetHeightCm))
    {
      // target position reached
      log_info("Reached target position");
      g_control = false;
    }
  }
}