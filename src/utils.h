#pragma once
#include <Arduino.h>

#ifdef ESP32
#include <WiFi.h>
#endif

#ifdef ESP8266
#include <ESP8266WiFi.h> 
#endif


String macToStr(const uint8_t *mac)
{
    String result;
    for (int i = 3; i < 6; ++i)
    {
        result += String(mac[i], 16);
        //if (i < 5)
        //    result += ':';
    }
    return result;
}

String composeClientID()
{
    uint8_t mac[6];
    WiFi.macAddress(mac);
    String clientId = "smartdesk-";
    clientId += macToStr(mac);
    return clientId;
}

void setupOTA()
{

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
}