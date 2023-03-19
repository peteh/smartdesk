#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <MqttDevice.h>
#include <esplog.h>

#include "utils.h"
#include "config.h"

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

bool g_outputUp = false;
bool g_outputDown = false;

double g_targetHeightCm = 80.;
double g_sensorHeightCm = 60.;
bool g_control = false;

WiFiClient net;
PubSubClient client(net);

const char *HOMEASSISTANT_STATUS_TOPIC = "homeassistant/status";
const char *HOMEASSISTANT_STATUS_TOPIC_ALT = "ha/status";

MqttDevice mqttDevice(composeClientID().c_str(), "Smart Desk", "Smart Desk Control OMT", "maker_pt");
MqttText mqttTargetHeight(&mqttDevice, "desktargetheight", "Target Height");

void moveUp()
{
  g_outputUp = true;
  g_outputDown = false;
  digitalWrite(OUTPUT_UP, g_outputUp);
  digitalWrite(OUTPUT_DOWN, g_outputDown);
}

void stop()
{
  log_info("Desk Stop!");
  g_outputUp = false;
  g_outputDown = false;
  digitalWrite(OUTPUT_UP, g_outputUp);
  digitalWrite(OUTPUT_DOWN, g_outputDown);
}

void moveDown()
{
  g_outputUp = false;
  g_outputDown = true;
  digitalWrite(OUTPUT_UP, g_outputUp);
  digitalWrite(OUTPUT_DOWN, g_outputDown);
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
  long duration = pulseIn(SENSOR_ECHO, HIGH, 1*1000*1000);
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

bool control(double sensor, double target)
{
  double distance = g_targetHeightCm - sensor;
  if (abs(distance) < TARGET_ACCURACY_CM)
  {
    g_control = false;
    log_info("Reached target position (target: %.2fcm, is: %.2f)", g_targetHeightCm, sensor);
    stop();
    return true;
  }
  else if (distance > 0) // need to go up
  {
    moveUp();
  }
  else if (distance < 0) // need to go down
  {
    moveDown();
  }
  return false;
}

void publishMqttState(MqttEntity *device, const char *state)
{
  client.publish(device->getStateTopic(), state);
}

void publishMqttState(MqttEntity *device, const int32_t state)
{
  char buffer[20];
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
  publishConfig(&mqttTargetHeight);

  delay(1000);
  // publish all initial states
  publishMqttState(&mqttTargetHeight, (uint32_t)g_sensorHeightCm);
}

uint32_t parseValue(const char *data, unsigned int length)
{
  // TODO length check
  char temp[32];
  strncpy(temp, data, length);
  return (uint32_t)strtoul(temp, NULL, 10);
}

void callback(char *topic, byte *payload, unsigned int length)
{
  log_info("Message arrived [%s]", topic);
  for (unsigned int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp(topic, mqttTargetHeight.getCommandTopic()) == 0)
  {
    uint32_t data = parseValue((char *)payload, length);
    setNewTarget(data);
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

  client.subscribe(mqttTargetHeight.getCommandTopic(), 1);

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
  mqttTargetHeight.setPattern("[0-9]+");
  mqttTargetHeight.setMaxLetters(3);
  mqttTargetHeight.setIcon("mdi:desk");

  Serial.begin(115200);

  pinMode(SENSOR_TRIGGER, OUTPUT);
  pinMode(SENSOR_ECHO, INPUT);

  pinMode(INPUT_UP, INPUT_PULLDOWN);
  pinMode(INPUT_DOWN, INPUT_PULLDOWN);

  pinMode(OUTPUT_UP, OUTPUT);
  pinMode(OUTPUT_DOWN, OUTPUT);

  digitalWrite(OUTPUT_UP, g_outputUp);
  digitalWrite(OUTPUT_DOWN, g_outputUp);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(composeClientID().c_str());
  WiFi.begin(wifi_ssid, wifi_pass);

  connectToWifi();
  client.setBufferSize(512);
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  log_info("Connected to SSID: %s", wifi_ssid);
  log_info("IP address: %s", WiFi.localIP().toString().c_str());

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    log_info("Start updating %s", type.c_str()); });
  ArduinoOTA.onEnd([]()
                   { log_info("End"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { log_info("Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    log_error("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      log_error("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      log_error("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      log_error("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      log_error("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      log_error("End Failed");
    } });
  ArduinoOTA.begin();

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
    if (g_inputUp)
    {
      moveUp();
    }
    else
    {
      stop();
    }
  }
  bool inputDown = digitalRead(INPUT_DOWN);
  if (inputUp != g_inputUp)
  {
    log_info("Down pressed: %d", inputDown);
    g_inputDown = inputDown;
    if (g_inputDown)
    {
      moveDown();
    }
    else
    {
      stop();
    }
  }

  if (g_control)
  {
    // TODO: read sensor value every x seconds and stop movement if not moving anymore
    double sensor = readSensor();
    if (control(sensor, g_targetHeightCm))
    {
      // target position reached
      log_info("Reached target position");
      g_control = false;
    }
  }
}