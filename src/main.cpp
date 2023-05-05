#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <MqttDevice.h>
#include <esplog.h>
#include <LittleFS.h>
#include "Adafruit_VL53L0X.h"

#include "utils.h"
#include "config.h"
#include "desk.h"

#define INPUT_UP 8
#define INPUT_DOWN 9

#define OUTPUT_UP 3
#define OUTPUT_DOWN 5

#define SENSOR_TRIGGER 16
#define SENSOR_ECHO 18

// when the position is in this range the control will stop
#define TARGET_ACCURACY_CM 5

// update only every x ms
const long MIN_UPDATE_RATE_MS = 1000;

bool g_inputUp = false;
bool g_inputDown = false;
Desk g_desk(OUTPUT_UP, OUTPUT_DOWN);

double g_targetHeightCm = 80.;
double g_sensorHeightCm = 60.;
double g_lastSensorHeightCm = 60.;
long g_lastSensorHeightUpdate = 0;
bool g_control = false;

#define NUM_PRESETS 3

uint16_t g_presets[NUM_PRESETS] = {0};

Adafruit_VL53L0X lox = Adafruit_VL53L0X();
namespace desk
{
  enum Preset
  {
    NONE,
    PRESET1,
    PRESET2,
    PRESET3
  };
}
desk::Preset g_preset = desk::Preset::NONE;

WiFiClient net;
PubSubClient client(net);

const char *HOMEASSISTANT_STATUS_TOPIC = "homeassistant/status";
const char *HOMEASSISTANT_STATUS_TOPIC_ALT = "ha/status";

MqttDevice mqttDevice(composeClientID().c_str(), "Smart Desk", "Smart Desk Control OMT", "maker_pt");
MqttText mqttHeight(&mqttDevice, "height", "Desk Height");
MqttSelect mqttPreset(&mqttDevice, "preset", "Desk Preset");
MqttText mqttConfigPresets[NUM_PRESETS] = {MqttText(&mqttDevice, "preset1", "Desk Preset 1"),
                                           MqttText(&mqttDevice, "preset2", "Desk Preset 2"),
                                           MqttText(&mqttDevice, "preset3", "Desk Preset 3")};

#define CONFIG_FILENAME "/config.json"

bool formatLittleFS()
{
  log_warn("need to format LittleFS: ");
  LittleFS.end();
  LittleFS.begin();
  log_info("Success: %d", LittleFS.format());
  return LittleFS.begin();
}

double readSensorUltrasonic()
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
  return distance;
}

double readSensorVL()
{
  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false); // pass in 'true' to get debug data printout!

  // if (measure.RangeStatus != 4) {  // phase failures have incorrect data
  //   Serial.print("Distance (mm): "); Serial.println(measure.RangeMilliMeter);
  // } else {
  //   Serial.println(" out of range ");
  // }
  return measure.RangeMilliMeter / 10.;
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
  for (uint8_t i = 0; i < NUM_PRESETS; i++)
  {
    g_presets[i] = doc["presets"][i] | g_presets[i];
  }

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
  JsonArray data = doc.createNestedArray("presets");
  for (uint8_t i = 0; i < NUM_PRESETS; i++)
  {
    data.add(g_presets[i]);
  }

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0)
  {
    log_error("Failed to write config to file");
  }

  // Close the file
  file.close();
}

void publishMqttPreset(MqttEntity *device, desk::Preset preset)
{
  if (preset == desk::Preset::NONE)
  {
    client.publish(device->getStateTopic(), "None");
  }
  else if (preset == desk::Preset::PRESET1)
  {
    client.publish(device->getStateTopic(), "1");
  }
  else if (preset == desk::Preset::PRESET2)
  {
    client.publish(device->getStateTopic(), "2");
  }
  else if (preset == desk::Preset::PRESET3)
  {
    client.publish(device->getStateTopic(), "3");
  }
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
  for (uint8_t i = 0; i < NUM_PRESETS; i++)
  {
    publishConfig(&mqttConfigPresets[i]);
  }

  delay(1000);

  // publish all initial states
  publishMqttState(&mqttHeight, (uint16_t)g_sensorHeightCm);
  publishMqttState(&mqttPreset, g_preset);
  for (uint8_t i = 0; i < NUM_PRESETS; i++)
  {
    publishMqttState(&mqttConfigPresets[i], g_presets[i]);
  }
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
      return;
    }
    uint16_t targetPreset = parseValue((char *)payload, length);
    if( targetPreset > 0 && targetPreset <= NUM_PRESETS)
    {
      log_info("Received Preset %d", targetPreset);
      setNewTarget((double)g_presets[targetPreset-1]);
    }
  }

  // check for preset saving commands
  for (uint8_t i = 0; i < NUM_PRESETS; i++)
  {
    if (strcmp(topic, mqttConfigPresets[i].getCommandTopic()) == 0)
    {
      uint16_t data = parseValue((char *)payload, length);
      g_presets[i] = data;
      log_info("Setting Preset %d to '%d'", i, g_presets[i]);
      saveSettings();
      publishMqttState(&mqttConfigPresets[i], g_presets[i]);
    }
  }

  // publish config when homeassistant comes online and needs the configuration again
  if (strcmp(topic, HOMEASSISTANT_STATUS_TOPIC) == 0 ||
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
  for (uint8_t i = 0; i < NUM_PRESETS; i++)
  {
    client.subscribe(mqttConfigPresets[i].getCommandTopic(), 1);
  }
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

desk::Preset calculatePreset(double sensorCm)
{
  if (abs(g_presets[0] - sensorCm) < g_desk.getTargetAccuracyCm())
  {
    return desk::Preset::PRESET1;
  }
  if (abs(g_presets[1] - sensorCm) < g_desk.getTargetAccuracyCm())
  {
    return desk::Preset::PRESET2;
  }
  if (abs(g_presets[2] - sensorCm) < g_desk.getTargetAccuracyCm())
  {
    return desk::Preset::PRESET3;
  }
  return desk::Preset::NONE;
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

  for (uint8_t i = 0; i < NUM_PRESETS; i++)
  {
    client.subscribe(mqttConfigPresets[i].getCommandTopic(), 1);
    mqttConfigPresets[i].setPattern("[0-9]+");
    mqttConfigPresets[i].setMaxLetters(3);
    mqttConfigPresets[i].setIcon("mdi:human-male-height");
    mqttConfigPresets[i].setEntityType(EntityCategory::CONFIG);
  }

  Serial.begin(115200);
  if (!lox.begin())
  {
    Serial.println(F("Failed to boot VL53L0X"));
    while (1)
      ;
  }

  pinMode(INPUT_UP, INPUT_PULLDOWN);
  pinMode(INPUT_DOWN, INPUT_PULLDOWN);

  pinMode(OUTPUT_UP, OUTPUT);
  pinMode(OUTPUT_DOWN, OUTPUT);

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

  // init sensor value
  lox.configSensor(Adafruit_VL53L0X::VL53L0X_SENSE_LONG_RANGE);
  g_sensorHeightCm = readSensorVL();

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
}

bool state = true;
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
  bool inputDown = digitalRead(INPUT_DOWN);

  if (inputUp)
  {
    g_control = false;
    g_desk.moveUp();
    g_sensorHeightCm = readSensorVL();
  }
  else if (inputDown)
  {
    g_control = false;
    g_desk.moveDown();
    g_sensorHeightCm = readSensorVL();
  }

  if (inputUp != g_inputUp)
  {
    log_info("Up pressed: %d", inputUp);
    g_inputUp = inputUp;
    if (!inputUp)
    {
      g_control = false;
      g_desk.stop();
    }
  }
  if (inputDown != g_inputDown)
  {
    log_info("Down pressed: %d", inputDown);
    g_inputDown = inputDown;
    g_control = false;
    if (!inputDown)
    {
      g_desk.stop();
    }
  }

  if (g_control)
  {
    // TODO: read sensor value every x seconds and stop movement if not moving anymore
    double sensorCm = readSensorVL();
    g_sensorHeightCm = sensorCm;
    log_debug("Sensor: %f", sensorCm);

    if (g_desk.controlLoop(sensorCm, g_targetHeightCm))
    {
      // target position reached
      log_info("Reached target position");
      g_control = false;
    }
  }
  // limit updates to configured frequency and update position and preset
  if (millis() - g_lastSensorHeightUpdate > MIN_UPDATE_RATE_MS && g_sensorHeightCm != g_lastSensorHeightCm)
  {
    desk::Preset preset = calculatePreset(g_sensorHeightCm);
    if (preset != g_preset)
    {
      log_info("Reached height of preset %d", g_preset);
      g_preset = preset;
      publishMqttPreset(&mqttPreset, g_preset);
    }
    publishMqttState(&mqttHeight, g_sensorHeightCm);
    g_lastSensorHeightCm = g_sensorHeightCm;
    g_lastSensorHeightUpdate = millis();
  }
}