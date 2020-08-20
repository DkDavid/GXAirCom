/*!
 * @file Screen.h
 *
 *
 */

extern struct SettingsData setting;
extern struct statusData status;



#ifndef __SCREEN_H__
#define __SCREEN_H__

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <GxEPD2_7C.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/NotoSans6pt7b.h>
#include <Fonts/NotoSansBold6pt7b.h>
#include <Fonts/gnuvarioe14pt7b.h>
#include <Fonts/gnuvarioe18pt7b.h>
#include <Fonts/gnuvarioe23pt7b.h>
#include "main.h"
#include <icons.h>
#include <string.h>

#define EINK_BUSY     33
#define EINK_RST      4
//#define EINK_DC       23
#define EINK_DC       32
#define EINK_CS       15
#define EINK_CLK      25
#define EINK_DIN      2

class Screen {
public:
  Screen(); //constructor
  bool begin(void);
  void end(void);
  void run(void); //has to be called cyclic
  void webUpdate(void);

private:
  bool bInit;
  void doInitScreen(void);
  void drawMainScreen(void);
  void drawFlightTime(int16_t x, int16_t y, int16_t width, int16_t height,uint32_t tTime);
  void drawValue(int16_t x, int16_t y, int16_t width, int16_t height,float value,uint8_t decimals);
  void drawCompass(int16_t x, int16_t y, int16_t width, int16_t height,float value);
  void drawBatt(int16_t x, int16_t y, int16_t width, int16_t height,uint8_t value);
  void drawSatCount(int16_t x, int16_t y, int16_t width, int16_t height,uint8_t value);
  void getTextPositions(int16_t *posx, int16_t *posy,int16_t x, int16_t y, int16_t width, int16_t height,String sText);
  void drawspeaker(int16_t x, int16_t y, int16_t width, int16_t height,uint8_t volume);
  String getWDir(float dir);
  uint8_t stepCount;
  struct screenMainData{
    uint8_t battPercent;
    float alt;
    float vario;
    float speed;
    float compass;
    uint8_t SatCount;
    uint32_t flightTime;
    uint8_t volume; //muting beeper
  };

};

#endif