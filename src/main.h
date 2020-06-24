#include <FanetLora.h>


#ifndef __MAIN_H__
#define __MAIN_H__

#define VERSION "0.9"
#define APPNAME "GXAirCom"

#define ARDUINO_RUNNING_CORE0 0
#define ARDUINO_RUNNING_CORE1 1

#define OUTPUT_SERIAL 0
#define OUTPUT_UDP 1

#define BAND868 0
#define BAND915 1

struct SettingsData{
  String myDevId; //my device-ID
  uint8_t band;
  uint8_t outputLK8EX1;
  uint8_t outputFLARM;
  uint8_t outputGPS;
  uint8_t outputFANET;
  String ssid; //WIFI SSID
  String password; //WIFI PASSWORD
  String PilotName; //Pilotname
  eFanetAircraftType AircraftType; //Aircrafttype
  bool bSwitchWifiOff3Min; //switch off wifi after 3min.
  uint32_t wifiDownTime;
  String UDPServerIP; //UDP-IP-Adress for sending Pakets
  uint16_t UDPSendPort; //Port of udp-server
  uint8_t outputMode; //output-mode
  uint8_t testMode;
};

struct statusData{
  String myIP; //my IP-Adress
  float vBatt; //battery-voltage
  uint8_t GPS_Fix;
  double GPS_Lat;
  double GPS_Lon;
  float GPS_alt;
  float GPS_speed;
  float GPS_course;
  uint8_t GPS_NumSat;
  float ClimbRate;
  uint16_t fanetTx;
  uint16_t fanetRx;
  uint8_t BoardVersion;
  uint32_t tGPSCycle;
};

#endif