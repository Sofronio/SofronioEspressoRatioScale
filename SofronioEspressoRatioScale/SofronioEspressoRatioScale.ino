/*
  2021-07-25 新增开机时校准，双击咖啡录入关机
  2021-07-26 新增开机按住清零校准，取消双击录入关机
  2021-08-02 新增开机修改sample，显示版本信息，手柄录入功能
  2021-08-07 v1.1 新增手柄录入功能
  2021-08-15 v1.1 去掉手柄录入（因为双头手柄含水量不一定），修复进入意式模式时未恢复参数，新增电量检测
  2021-09-01 v1.2 重新加入手柄录入 修复sample不可更改bug
  2021-09-03 v1.3 修复切换到意式模式直接计时问题 修复录入可能产生负值问题
  2021-10-02 v1.4 修复切换到意式模式 下液计时不清零问题
*/
//#include "stdlib.h"
#include <Arduino.h>
#define HW_I2C //oled连接方式
#if defined(ESP8266)|| defined(ESP32) || defined(AVR)
#include <EEPROM.h>
#endif
#include <HX711_ADC.h>
#include <AceButton.h>
using namespace ace_button;
#include <StopWatch.h>

//针脚定义
#if defined(AVR)
#define BUZZER        12
#define OLED_SCL      6
#define OLED_SDA      7
#define OLED_RST      8
#define OLED_DC       9
#define OLED_CS       10

#define BUTTON_SET    2
#define BUTTON_PLUS   3
#define BUTTON_MINUS  4
#define BUTTON_TARE   5

#define HX711_SDA     A2
#define HX711_SCL     A3
#define BATTERY_LEVEL A0
#define USB_LEVEL     A1
#endif

void(* resetFunc) (void) = 0;//重启函数

//电子秤校准参数
float calibrationValue;
int sampleNumber = 0; //0-7
int sample[] = {1, 2, 4, 8, 16, 32, 64, 128};

//HX711模数转换初始化
HX711_ADC scale(HX711_SDA, HX711_SCL);

//模式标识
bool boolEspresso = false; //浓缩模式标识
bool boolCalibration = false; //校准标识
bool boolSetSample = false;
bool boolShowInfo = false;
bool boolSetPortaFilterWeight = false;
bool boolPortaFilter = false;
bool boolReadyToBrew = false; //准备冲煮并计时

//EEPROM地址列表
int address_calibrationValue = 0;
int address_sampleNumber = sizeof(calibrationValue);
int address_PORTAFILTER_WEIGHT = sizeof(calibrationValue) + sizeof(sampleNumber);

unsigned long t_cal = 0;
int button_cal_status = 0;
float GRIND_COFFEE_WEIGHT = 0.0; //咖啡粉重量
float PORTAFILTER_WEIGHT = 0.0; //咖啡手柄重量

char coffee[10];            //咖啡重量
int decimalPrecision = 1;   //小数精度 0.1g/0.01g

const int Margin_Top = 0;
const int Margin_Bottom = 0;
const int Margin_Left = 0;
const int Margin_Right = 0;


StopWatch stopWatch;
char minsec[] = "00:00";
char *sec2minsec(int n) {
  int minute = 0;
  int second = 0;
  if (n < 99 * 60 + 60) {
    if (n >= 60) {
      minute = n / 60;
      n = n % 60;
    }
    second = n;
  } else {
    minute = 99;
    second = 59;
  }
  snprintf(minsec, 6, "%02d:%02d", minute, second);
  return minsec;
}

char *sec2sec(int n) {
  int second = 0;
  if (n < 99) {
    second = n;
  }
  else
    second = 99;
  snprintf(minsec, 3, "%02d", second);
  return minsec;
}

//电子秤参数和计时点
bool fixWeightZero = false;
char wu[10];
char r[10];
unsigned long t0 = 0;               //开始萃取打点
unsigned long t1 = 0;               //下液第一滴打点
unsigned long t2 = 0;               //下液结束打点
float w0 = 0.0; //咖啡粉重（g）
float w1 = 0.0;   //下液重量（g）
float r0 = 0.0;   //粉水比 w1/w0
int tareCounter = 0; //不稳计数器
int defaultPowderWeight = 20;//默认咖啡粉重量

float aWeight = 0;          //稳定状态比对值（g）
float aWeightDiff = 0.05;    //稳定停止波动值（g）
float atWeight = 0;         //自动归零比对值（g）
float atWeightDiff = 0.3;   //自动归零波动值（g）
float asWeight = 0;         //下液停止比对值（g）
float asWeightDiff = 0.1;   //下液停止波动值（g）
float rawWeight = 0.0;      //原始读出值（g）

unsigned long autoTareMarker = 0;       //自动归零打点
unsigned long autoStopMarker = 0;       //下液停止打点
unsigned long scaleStableMarker = 0;    //稳定状态打点
unsigned long timeOutMarker = 0;        //超时打点
unsigned long t = 0;                  //最后一次重量输出打点
unsigned long oledRefreshMarker = 0;   //最后一次oled刷新打点
unsigned long lastButtonPressMarker = 0;   //最后一次按钮计时

const int autoShutDownTimer = 10 * 60 * 1000; //10分钟未按键自动关机
const int autoTareInterval = 500;       //自动归零检测间隔（毫秒）
const int autoStopInterval = 500;       //下液停止检测间隔（毫秒）
const int scaleStableInterval = 500;   //稳定状态监测间隔（毫秒）
const int timeOutInterval = 10 * 1000;      //超时检测间隔（毫秒）
int oledPrintInterval = 0;     //oled刷新间隔（毫秒）
const int serialPrintInterval = 0;  //称重输出间隔（毫秒）
//固定参数
float fix_ = 0.0;
float fix_ratio = 0.0;
char *fix_time;

//按钮配置
ButtonConfig config1;
AceButton buttonSet(&config1);
AceButton buttonPlus(&config1);
AceButton buttonMinus(&config1);
AceButton buttonTare(&config1);

//显示屏初始化 https://github.com/olikraus/u8g2/wiki/u8g2reference
//设置字体 https://github.com/olikraus/u8g2/wiki/fntlistall
#define FONT_L u8g2_font_logisoso24_tn
#define FONT_M u8g2_font_fub14_tr
#define FONT_S u8g2_font_helvR12_tr
#define FONT_BATTERY u8g2_font_battery19_tn
char* c_battery = "0";//电池字符 0-5有显示
char* c_batteryTemp = "0";
unsigned long t_battery = 0; //电池充电循环判断打点
int i_battery = 0; //电池充电循环变量
int batteryRefreshTareInterval = 3 * 1000; //10秒刷新一次电量显示
unsigned long t_batteryRefresh = 0; //电池充电循环判断打点
//float vRef = 4.72; for arduino nano 328p (not old)
float vRef = 5.13; //for arduino pro mini

int FONT_M_HEIGHT;
int FONT_S_HEIGHT;
int FONT_L_HEIGHT;
int displayRotation = 0; //旋转方向 0 1 2 3 : 0 90 180 270

#include <U8g2lib.h>
#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif
#if defined(AVR)
#ifdef HW_I2C
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
#endif
//U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R2, /* reset=*/ U8X8_PIN_NONE);
#ifdef SW_SPI_OLD
U8G2_SSD1306_128X64_NONAME_1_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);
#endif
#ifdef SW_SPI
U8G2_SSD1306_128X64_NONAME_1_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ OLED_SCL, /* data=*/ OLED_SDA, /* cs=*/ OLED_CS, /* dc=*/ OLED_DC, /* reset=*/ OLED_RST);
#endif
#ifdef SW_I2C
U8G2_SSD1306_128X64_NONAME_1_SW_I2C u8g2(U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* reset=*/ U8X8_PIN_NONE);
#endif
#endif


//文本对齐 AC居中 AR右对齐 AL左对齐 T为要显示的文本
#define LCDWidth  u8g2.getDisplayWidth()
#define LCDHeight u8g2.getDisplayHeight()
#define AC(T)     ((LCDWidth - u8g2.getStrWidth(T)) / 2 - Margin_Left - Margin_Right)
#define AR(T)     (LCDWidth -  u8g2.getStrWidth(T) - Margin_Right)
#define AL(T)     (u8g2.getStrWidth(T) + Margin_Left)

#define AM()     ((LCDHeight + u8g2.getMaxCharHeight()) / 2 - Margin_Top - Margin_Bottom)
#define AB()     (LCDHeight -  u8g2.getMaxCharHeight() - Margin_Bottom)
#define AT()     (u8g2.getMaxCharHeight() + Margin_Top)

//自定义trim消除空格
char *ltrim(char *s) {
  while (isspace(*s)) s++;
  return s;
}

char *rtrim(char *s) {
  char* back = s + strlen(s);
  while (isspace(*--back));
  *(back + 1) = '\0';
  return s;
}

char *trim(char *s) {
  return rtrim(ltrim(s));
}

void button_init() {
  pinMode(BUTTON_SET, INPUT_PULLUP);
  pinMode(BUTTON_PLUS, INPUT_PULLUP);
  pinMode(BUTTON_MINUS, INPUT_PULLUP);
  pinMode(BUTTON_TARE, INPUT_PULLUP);
  buttonSet.init(BUTTON_SET);
  buttonPlus.init(BUTTON_PLUS);
  buttonMinus.init(BUTTON_MINUS);
  buttonTare.init(BUTTON_TARE);
  config1.setEventHandler(handleEvent1);
  config1.setFeature(ButtonConfig::kFeatureClick);
  config1.setFeature(ButtonConfig::kFeatureLongPress);
  config1.setFeature(ButtonConfig::kFeatureRepeatPress);
  config1.setRepeatPressInterval(10);
}

void handleEvent1(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  int pin = button->getPin();
  switch (eventType) {
    case AceButton::kEventPressed:
      switch (pin) {
        case BUTTON_SET:
          beep(1, 100);
          if (boolSetPortaFilterWeight)
            setPortaFilterWeight(1);
          else if (boolShowInfo)
            boolShowInfo = false;
          else
            buttonSet_Clicked();
          break;
        case BUTTON_PLUS:
          beep(1, 100);
          if (!boolEspresso)
            boolPortaFilter = !boolPortaFilter;//是否减去咖啡手柄重量
          if (boolSetSample)
            sampleNumber++;
          else if (boolShowInfo)
            boolShowInfo = false;
          else
            buttonPlus_Clicked();
          break;
        case BUTTON_MINUS:
          beep(1, 100);
          if (boolSetSample)
            sampleNumber--;
          else if (boolShowInfo)
            boolShowInfo = false;
          else
            buttonMinus_Clicked();
          break;
        case BUTTON_TARE:
          beep(1, 100);
          if (boolSetSample)
            setSample(1);//保存sample设定
          else if (boolShowInfo)
            boolShowInfo = false;
          else if (boolCalibration)
            button_cal_status++;
          else
            buttonTare_Clicked();
          break;
      }
      break;
    case AceButton::kEventLongPressed:
      switch (pin) {
        case BUTTON_SET:
          beep(1, 100);
          if (boolEspresso == true) //意式模式下 长按回归普通模式
            boolEspresso = false;
          break;
        case BUTTON_PLUS:
          beep(1, 100);
          break;
        case BUTTON_TARE:
          beep(1, 100);
          if (boolCalibration)
            resetFunc();
          break;
      }
      break;
    case AceButton::kEventRepeatPressed:
      switch (pin) {
        case BUTTON_PLUS:
          buttonPlus_Clicked();
          break;
        case BUTTON_MINUS:
          buttonMinus_Clicked();
          break;
      }
      break;
  }
}
void buttonSet_Clicked() {
  if (boolEspresso) {
    if (stopWatch.isRunning() == false) {
      //无论何时都可以操作的时钟
      if (stopWatch.elapsed() == 0)
      {
        //初始态 时钟开始

        delay(500);
        stopWatch.start();
      }
      if (stopWatch.elapsed() > 0)
      {
        //停止态 时钟清零
        stopWatch.reset();
      }
    }
    else {
      //在计时中 按一下则结束计时 停止冲煮 （固定参数回头再说）
      stopWatch.stop();
    }
  }
  else {
    //普通录入 切换到意式模式
    if (boolPortaFilter) {
      //去掉手柄重量模式
      GRIND_COFFEE_WEIGHT = rawWeight - PORTAFILTER_WEIGHT;
      boolPortaFilter = false;
      if (GRIND_COFFEE_WEIGHT < 3)
        GRIND_COFFEE_WEIGHT = defaultPowderWeight; //不足3g 录入为默认值（20g）配合手柄模式使用
    }
    if (rawWeight < 3)
      GRIND_COFFEE_WEIGHT = defaultPowderWeight; //不足3g 录入为默认值（20g）    
    else
      GRIND_COFFEE_WEIGHT = rawWeight;    
    initEspresso();      
    boolReadyToBrew = false;
    stopWatch.stop();
    stopWatch.reset();
    scale.tareNoDelay();  
    boolEspresso = true;
  }
}

void buttonPlus_Clicked() {
  if (boolEspresso) {
    //意式模式 按一下+0.1g
    GRIND_COFFEE_WEIGHT = GRIND_COFFEE_WEIGHT + 0.1;
  }
}

void buttonMinus_Clicked() {
  if (boolEspresso) {
    //意式模式 按一下-0.1g
    if (GRIND_COFFEE_WEIGHT - 0.1 > 0)
      GRIND_COFFEE_WEIGHT = GRIND_COFFEE_WEIGHT - 0.1;
  }
  else {
    if (decimalPrecision == 1)
      decimalPrecision = 2;
    else
      decimalPrecision = 1;
  }
}

void buttonTare_Clicked() {
  if (boolEspresso) {
    if (stopWatch.isRunning() == false) {
      //意式模式归零
      //不在计时中 也就是说没有冲煮 因此可以重量归零 时间归零
      fixWeightZero = true;
      if (!boolEspresso)
        GRIND_COFFEE_WEIGHT = 0;
      stopWatch.reset();
      t0 = 0;
      t1 = 0;
      t2 = 0;
      scale.tareNoDelay();
    }
    else {
      //在计时中 按一下则结束计时 停止冲煮 （固定参数回头再说）
      stopWatch.stop();
    }
  }
  else {
    //普通归零
    fixWeightZero = true;
    scale.tareNoDelay();
  }
}

void setPortaFilterWeight(int input) {
  float portaFilterWeight = 0;
  scale.setSamplesInUse(4);
  while (boolSetPortaFilterWeight) {
    buttonTare.check();
    buttonSet.check();
    switch (input) {
      case 0:
        static boolean newDataReady = 0;
        static boolean scaleStable = 0;
        if (scale.update()) newDataReady = true;

        if (newDataReady) {
          portaFilterWeight = scale.getData();
          newDataReady = 0;
        }
        char c_temp[7];
        PORTAFILTER_WEIGHT = portaFilterWeight;
        dtostrf(PORTAFILTER_WEIGHT, 7, decimalPrecision, c_temp);
        refreshOLED("Set PortaFilter", "Weight", trim(c_temp));
        break;
      case 1:
#if defined(ESP8266)|| defined(ESP32)
        EEPROM.begin(512);
#endif
        EEPROM.put(address_PORTAFILTER_WEIGHT, PORTAFILTER_WEIGHT);
#if defined(ESP8266)|| defined(ESP32)
        EEPROM.commit();
#endif
        refreshOLED("Saved");
        delay(1000);
        boolSetPortaFilterWeight = false;
        resetFunc();
        break;
    }
  }
  Serial.print(F("PortaFilterWeight set to: "));
  Serial.print(PORTAFILTER_WEIGHT);
}

void setSample(int input) {
  static const char* sampleText[] = {"1", "2", "4", "8", "16", "32", "64", "128"};
  while (boolSetSample) {
    buttonPlus.check();//左移动
    buttonTare.check();//右移动
    buttonMinus.check();//确认
    if (sampleNumber < 0)
      sampleNumber = 7;
    if (sampleNumber > 7)
      sampleNumber = 0;
    switch (input) {
      case 0: //左右移动
        refreshOLED("Set Sample", sampleText[sampleNumber]);
        break;
      case 1: //保存
#if defined(ESP8266)|| defined(ESP32)
        EEPROM.begin(512);
#endif
        EEPROM.put(address_sampleNumber, sampleNumber);
#if defined(ESP8266)|| defined(ESP32)
        EEPROM.commit();
#endif
        refreshOLED("Saved", sampleText[sampleNumber]);

        delay(1000);
        boolSetSample = false;
        break;
    }
  }
  Serial.print(F("sampleNumber: "));
  Serial.print(sampleNumber);
  Serial.print(F(" sample set to"));
  Serial.println(sample[sampleNumber]);
}

void cal() {
  char* calval = "";
  if (button_cal_status == 1) {
    scale.setSamplesInUse(16);
    Serial.println(F("***"));
    Serial.println(F("Start calibration:"));
    Serial.println(F("Place the load cell an a level stable surface."));
    Serial.println(F("Remove any load applied to the load cell."));
    Serial.println(F("Press Button to set the tare offset."));
    refreshOLED("Press", "Tare Button");

    boolean _resume = false;
    while (_resume == false) {

      scale.update();
      buttonTare.check();
      if (button_cal_status == 2) {
        Serial.println(F("Hands off Taring...3"));
        refreshOLED("Hands off", "Tare in...3");

        delay(1000);
        refreshOLED("Hands off", "Tare in...2");

        delay(1000);
        refreshOLED("Hands off", "Tare in...1");

        delay(1000);
        refreshOLED("Taring...");
        scale.tare();
        Serial.println(F("Tare done"));
        refreshOLED("Tare done");

        delay(1000);
        _resume = true;
      }
    }
    Serial.println(F("Now, place your known mass on the loadcell."));
    Serial.println(F("Then send the weight of this mass (i.e. 100.0) from serial monitor."));
    refreshOLED("Put 100g");

    float known_mass = 0;
    _resume = false;
    while (_resume == false) {

      scale.update();
      buttonTare.check();
      if (button_cal_status == 3) {
        known_mass = 100.0;
        if (known_mass != 0) {
          Serial.print(F("Known mass is: "));
          Serial.println(known_mass);
          refreshOLED("Hands off", "Cal in 3");

          delay(1000);
          refreshOLED("Hands off", "Cal in 2");

          delay(1000);
          refreshOLED("Hands off", "Cal in 1");

          delay(1000);
          refreshOLED("Calibrating...");
          _resume = true;
        }
      }
    }
    scale.refreshDataSet(); //refresh the dataset to be sure that the known mass is measured correct
    calibrationValue = scale.getNewCalibration(known_mass); //get the new calibration value
    Serial.print(F("New calibration value: "));
    Serial.println(calibrationValue);
#if defined(ESP8266)|| defined(ESP32)
    EEPROM.begin(512);
#endif
    EEPROM.put(address_calibrationValue, calibrationValue);
#if defined(ESP8266)|| defined(ESP32)
    EEPROM.commit();
#endif
    dtostrf(calibrationValue, 10, 2, calval);
    refreshOLED("Cal value", trim(calval));

    delay(1000);
  }
  boolCalibration = false;
}


void refreshOLED(char* input) {

  u8g2.firstPage();
  u8g2.setFont(FONT_M);
  do {
    //1行
    //FONT_M = u8g2_font_fub14_tn;
    u8g2.drawStr(AC(input), AM() - 5, input);
  } while ( u8g2.nextPage() );
}

void refreshOLED(char* input1, char* input2) {

  u8g2.firstPage();
  u8g2.setFont(FONT_M);
  do {
    //2行
    //FONT_M = u8g2_font_fub14_tn;
    u8g2.drawStr(AC(input1), FONT_M_HEIGHT, input1);
    u8g2.drawStr(AC(input2), LCDHeight - 5, input2);
  } while ( u8g2.nextPage() );
}

void refreshOLED(char* input1, char* input2, char* input3) {

  u8g2.firstPage();
  u8g2.setFont(FONT_S);
  do {
    //2行
    //FONT_M = u8g2_font_fub14_tn;
    u8g2.drawStr(AC(input1), FONT_S_HEIGHT, input1);
    u8g2.drawStr(AC(input2), AM(), input2);
    u8g2.drawStr(AC(input3), LCDHeight, input3);
  } while ( u8g2.nextPage() );
}

void initEspresso() {    
  stopWatch.reset();
  t0 = 0;               //开始萃取打点
  t1 = 0;               //下液第一滴打点
  t2 = 0;               //下液结束打点
  tareCounter = 0; //不稳计数器
  autoTareMarker = 0;       //自动归零打点
  autoStopMarker = 0;       //下液停止打点
  scaleStableMarker = 0;    //稳定状态打点
  timeOutMarker = 0;        //超时打点
  t = 0;                  //最后一次重量输出打点
}

void setup() {

  delay(50); //有些单片机会重启两次
#if defined(ESP8266)
  WiFi.mode(WIFI_OFF);
#endif
#if defined(ESP32)
  WiFi.mode( WIFI_MODE_NULL );
#endif
  Serial.begin(115200);
  while (!Serial); //等待串口就绪

  button_init();
  pinMode(BUZZER, OUTPUT);
  beep(1, 100);
  u8g2.begin();
  u8g2.setFont(FONT_S);
  FONT_S_HEIGHT = u8g2.getMaxCharHeight();
  u8g2.setFont(FONT_M);
  u8g2.setContrast(255);
  FONT_M_HEIGHT = u8g2.getMaxCharHeight();
  //char* welcome = "soso E.R.S"; //欢迎文字
  //refreshOLED(welcome);
  refreshOLED("Sofronio's", "Espresso", "Ratio Scale");
  stopWatch.setResolution(StopWatch::SECONDS);
  stopWatch.start();
  stopWatch.reset();

  button_cal_status = 1;//校准状态归1
  unsigned long stabilizingtime = 500;  //去皮时间(毫秒)，增加可以提高去皮精确度
  boolean _tare = true;                  //电子秤初始化去皮，如果不想去皮则设为false
  scale.begin();
  scale.start(stabilizingtime, _tare);

  //检查手柄重量合法性
#if defined(ESP8266)|| defined(ESP32)
  EEPROM.begin(512);
#endif
  EEPROM.get(address_PORTAFILTER_WEIGHT, PORTAFILTER_WEIGHT);
  Serial.print(F("PORTAFILTER_WEIGHT :"));
  Serial.println(PORTAFILTER_WEIGHT);
  if (isnan(PORTAFILTER_WEIGHT)) {
    PORTAFILTER_WEIGHT = 0; //手柄重量为0
    //保存0值
#if defined(ESP8266)|| defined(ESP32)
    EEPROM.begin(512);
#endif
    EEPROM.put(address_PORTAFILTER_WEIGHT, sampleNumber);
#if defined(ESP8266)|| defined(ESP32)
    EEPROM.commit();
#endif
  }

  //检查校准值合法性
#if defined(ESP8266)|| defined(ESP32)
  EEPROM.begin(512);
#endif
  EEPROM.get(address_calibrationValue, calibrationValue);
  Serial.print(F("readEeprom(0, calibrationValue);"));
  Serial.println(calibrationValue);
  if (isnan(calibrationValue)) {
    boolCalibration = true; //让按钮进入校准状态3
    cal(); //无有效读取，进入校准模式
  }
  else
    scale.setCalFactor(calibrationValue);  //设定校准值

  //检查sample值合法性
#if defined(ESP8266)|| defined(ESP32)
  EEPROM.begin(512);
#endif
  EEPROM.get(address_sampleNumber, sampleNumber);
  Serial.print(F("eepromSampleNumber: "));
  Serial.println(sampleNumber);
  if (isnan(sampleNumber)) {
    boolSetSample = true;
    sampleNumber = 3;//读取失败 默认值为3 对应sample为8
    setSample(0);
  }

  if (digitalRead(BUTTON_SET) == LOW) {
    boolSetSample = true;
    setSample(0);
  }

  if (digitalRead(BUTTON_PLUS) == LOW) {
    //录入手柄重量
    boolSetPortaFilterWeight = true;
    setPortaFilterWeight(0);
  }

  if (digitalRead(BUTTON_MINUS) == LOW) {
    boolShowInfo = true;
    showInfo();

    delay(2000);
  }

  if (digitalRead(BUTTON_TARE) == LOW) {
    boolCalibration = true; //让按钮进入校准状态3
    cal(); //无有效读取，进入校准模式
  }
  scale.setCalFactor(calibrationValue);
  scale.setSamplesInUse(sample[sampleNumber]);
}

void showInfo() {
  char* scaleInfo[] = {/*版本号*/ "Version: 1.4", /*编译日期*/ "Build: 20211002", /*序列号*/ "S/N: xxxxxxxx"}; //序列号
  refreshOLED(scaleInfo[0], scaleInfo[1], scaleInfo[2]);
  while (boolShowInfo) {

    buttonSet.check();
    buttonPlus.check();
    buttonMinus.check();
    buttonSet.check();
  }
}

void espressoScale() {
  if (GRIND_COFFEE_WEIGHT == 0)
    GRIND_COFFEE_WEIGHT = 20.0;
  static boolean newDataReady = 0;
  static boolean scaleStable = 0;
  if (scale.update()) newDataReady = true;
  if (newDataReady) {
    if (millis() > t + serialPrintInterval) {
      rawWeight = scale.getData();
      newDataReady = 0;
      t = millis();
      //-0.0 -> 0.0 正负符号稳定
      if (rawWeight > -0.15 && rawWeight < 0)
        rawWeight = 0.0;
      //Serial.print(trim(wu));
      if (millis() > scaleStableMarker + scaleStableInterval) {
        //稳定判断
        scaleStableMarker = millis();//重量稳定打点
        if (abs(aWeight - rawWeight) < aWeightDiff) {
          scaleStable = true; //称已经稳定
          aWeight = rawWeight; //稳定重量aWeight
          if (millis() > autoTareMarker + autoTareInterval) {
            if (t0 > 0 && tareCounter > 3) {
              //t0>0 已经开始萃取 tareCounter>3 忽略前期tare时不稳定
              if (t2 == 0) { //没有给t2计时过
                t2 = millis(); //萃取完成打点
              }
              if (t2 - t1 < 5000) { //最终下液到稳定时间不到5秒 继续计时
                t2 = 0;
              }
              else if (boolReadyToBrew) { 
                //正常过程 最终下液到稳定时间大于5秒 
                stopWatch.stop();
                //萃取完成 单次固定液重
                //Serial.println(F("萃取完成 单次固定液重"));
                w1 = rawWeight;
                beep(3, 50);
                boolReadyToBrew = false;
              }
            }
            if (stopWatch.elapsed() == 0) {
              //秒表没有运行
              autoTareMarker = millis(); //自动清零计时打点
              if (rawWeight > 30 ) { //大于30g说明放了杯子 3g是纸杯
                scale.tare();
                beep(1, 100);
                tareCounter = 0;
                t2 = 0;
                t1 = 0;
                if (boolReadyToBrew) {
                  //已经准备冲煮状态 才开始计时
                  stopWatch.reset();
                  stopWatch.start();
                }
                else {
                  //没有准备好冲煮 则第一次清零不计时 并进入准备冲煮状态
                  boolReadyToBrew = true;
                }
                scaleStable = false;
                t0 = millis();
                //Serial.println(F("正归零 开始计时 取消稳定"));
              }
              //时钟为零，负重量稳定后归零，时钟不变
              if (rawWeight < -0.5) { //负重量状态
                scale.tare();
                //负归零后 进入准备冲煮状态 【下次】放了杯子后 清零并计时
                boolReadyToBrew = true;
              }
            }
            atWeight = rawWeight;
          }
        }
        else { //称不稳定
          scaleStable = false;
          if (t0 > 0) {
            //过滤tare环节的不稳
            if (tareCounter <= 3)
              tareCounter ++; ///tare后 遇到前2次不稳 视为稳定
            else {
              //tareCounter > 3 //视为开始萃取
              //萃取开始 下液重量开始计算
              w1 = rawWeight;
              if (t1 == 0) {
                t1 = millis();//第一滴下液
                //Serial.print("第一滴时间");
                //Serial.println((t1 - t0) / 1000);
              }
            }
          }
#ifdef DEBUG
          //Serial.print(F("不稳 aWeight"));
          //Serial.print(aWeight);
          //Serial.print(F(" rawWeight "));
          //Serial.print(rawWeight);
          //Serial.print(F(" 差绝对值"));
          //Serial.print(abs(aWeight - rawWeight));
          //Serial.print(F(" 设定差"));
          //Serial.println(aWeightDiff);
#endif
          aWeight = rawWeight;
          //不稳 为负 停止计时
          //stopWatch.stop();
        }
      }
    }
  }

  //记录咖啡粉时，将重量固定为0
  if (scale.getTareStatus()) {
    beep(2, 50);
    boolReadyToBrew = true;
    fixWeightZero = false;
  }
  if (fixWeightZero)
    rawWeight = 0.0;

  dtostrf(rawWeight, 7, decimalPrecision, wu);

  float ratio_temp = rawWeight / GRIND_COFFEE_WEIGHT;
  if (ratio_temp < 0)
    ratio_temp = 0.0;
  if (GRIND_COFFEE_WEIGHT < 0.1)
    ratio_temp = 0.0;
  dtostrf(ratio_temp, 7, decimalPrecision, r);
}

void pureScale() {
  static boolean newDataReady = 0;
  static boolean scaleStable = 0;
  if (scale.update()) newDataReady = true;

  if (newDataReady) {
    rawWeight = scale.getData();
    newDataReady = 0;
    if (rawWeight >= -0.15 && rawWeight <= 0)
      rawWeight = 0.0;
    //dtostrf(rawWeight, 7, decimalPrecision, wu);
    //Serial.print(trim(wu));
  }

  //记录咖啡粉时，将重量固定为0
  if (scale.getTareStatus()) {
    beep(2, 50);
    fixWeightZero = false;
  }
  if (fixWeightZero)
    rawWeight = 0.0;
  if (boolPortaFilter == true)
    dtostrf(rawWeight - PORTAFILTER_WEIGHT, 7, decimalPrecision, wu);
  else
    dtostrf(rawWeight, 7, decimalPrecision, wu);


  float ratio_temp = rawWeight / GRIND_COFFEE_WEIGHT;
  if (ratio_temp < 0)
    ratio_temp = 0.0;
  if (GRIND_COFFEE_WEIGHT < 0.1)
    ratio_temp = 0.0;
  dtostrf(ratio_temp, 7, decimalPrecision, r);
}

void checkBattery() {
  float batteryVoltage = analogRead(BATTERY_LEVEL) * vRef;
  float usbVoltage = analogRead(USB_LEVEL) * vRef;
  float perc = map(batteryVoltage, 3.6 * 1023, 4.1 * 1023, 0, 100);
  int i_icon = map(perc, 0, 100, 0, 5);
  switch (i_icon) {
    case 0:
      c_battery = "0";
      break;
    case 1:
      c_battery = "1";
      break;
    case 2:
      c_battery = "2";
      break;
    case 3:
      c_battery = "3";
      break;
    case 4:
      c_battery = "4";
      break;
    case 5:
      c_battery = "5";
      break;
    default:
      c_battery = "5";
      break;
  }
  if (usbVoltage > 4.3 * 1023) {
    c_battery = "c";
  }
#ifdef DEBUG
  Serial.print(c_battery);
  Serial.println(batteryVoltage);
#endif
}

void loop() {

  buttonSet.check();
  buttonPlus.check();
  buttonMinus.check();
  buttonTare.check();
  checkBattery();
  if (boolEspresso) {
    espressoScale();
  }
  else {
    pureScale();
  }
  Serial.println(trim(wu));
  if (millis() > oledRefreshMarker + oledPrintInterval)
  {
    //达到设定的oled刷新频率后进行刷新
    oledRefreshMarker = millis();
    int x = 0;
    int y = 0;
    char ratio[30];
    sprintf(ratio, "1:%s", trim(r));
    char coffeepowder[30];
    dtostrf(GRIND_COFFEE_WEIGHT, 7, decimalPrecision, coffeepowder);
    u8g2.firstPage();
    if (boolEspresso) {
      do {

        if (displayRotation == 0) {
          u8g2.setFontDirection(0);
          u8g2.setDisplayRotation(U8G2_R0);
          u8g2.setFont(FONT_M);
          x = Margin_Left;
          y = FONT_M_HEIGHT + Margin_Top - 5;
          u8g2.drawStr(x, y, trim(wu));
          //u8g2.drawStr(AR(sec2minsec(stopWatch.elapsed())), y, sec2minsec(stopWatch.elapsed()));
          u8g2.drawStr(AR(sec2sec(stopWatch.elapsed())), y, sec2sec(stopWatch.elapsed()));

          if (t1 > 0 && t1 - t0 > 0) { //有下液了
            int t1_num = (t1 - t0) / 1000;
            u8g2.drawStr(70, y, sec2sec(t1_num));
          }

          y = y + FONT_M_HEIGHT;
          //u8g2.setFont(FONT_S);
          u8g2.drawStr(AC("Espresso"), y + 1, "Espresso");

          u8g2.setFont(FONT_M);
          x = Margin_Left;
          y = LCDHeight - Margin_Bottom;
          u8g2.drawStr(x, y, trim(coffeepowder));
          u8g2.drawStr(AR(trim(ratio)), y, trim(ratio));
          u8g2.setFontDirection(1);
          u8g2.setFont(FONT_BATTERY);
          if (c_battery == "c") {
            if (i_battery == 6)
              i_battery = 0;
            if (millis() > t_battery + 500) {
              i_battery++;
              t_battery = millis();
            }
            String(i_battery).toCharArray(c_battery, 2);
            u8g2.drawStr(108, 29, c_battery);
          }
          else {
            if (millis() > t_batteryRefresh + batteryRefreshTareInterval) {
              c_batteryTemp = c_battery;
              t_batteryRefresh = millis();
            }
            u8g2.drawStr(108, 29, c_batteryTemp);
          }
#ifdef DEBUG
          u8g2.setFontDirection(0);
          u8g2.setFont(FONT_S);
          float batteryVoltage = analogRead(BATTERY_LEVEL) * vRef / 1023;
          char c_votage[10];
          String(batteryVoltage).toCharArray(c_votage, 10);
          u8g2.drawStr(AR(c_votage), 64, trim(c_votage));
          Serial.print("v");
          Serial.println(c_votage);
#endif
        }
      } while ( u8g2.nextPage() );
    }
    else {
      //纯称重
      do {
        u8g2.setDisplayRotation(U8G2_R0);
        u8g2.setFont(FONT_L);
        FONT_L_HEIGHT = u8g2.getMaxCharHeight();
        x = AC(trim(wu));
        y = AM();
        u8g2.drawStr(x, y - 5, trim(wu));
        u8g2.setFontDirection(1);
        u8g2.setFont(FONT_BATTERY);
        if (c_battery == "c") {
          if (i_battery == 6)
            i_battery = 0;
          if (millis() > t_battery + 500) {
            i_battery++;
            t_battery = millis();
          }
          String(i_battery).toCharArray(c_battery, 2);
          u8g2.drawStr(108, 7, c_battery);
        }
        else {
          if (millis() > t_batteryRefresh + batteryRefreshTareInterval) {
            c_batteryTemp = c_battery;
            t_batteryRefresh = millis();
          }
          u8g2.drawStr(108, 7, c_batteryTemp);
        }
        u8g2.setFontDirection(0);
#ifdef DEBUG
        u8g2.setFont(FONT_S);
        float batteryVoltage = analogRead(BATTERY_LEVEL) * vRef / 1023;
        char c_votage[10];
        String(batteryVoltage).toCharArray(c_votage, 10);
        u8g2.drawStr(AR(c_votage), 64, trim(c_votage));
        Serial.print("v");
        Serial.println(c_votage);
#endif
      } while ( u8g2.nextPage() );
    }
  }
}

void beep(int times, int duration) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(duration);
    digitalWrite(BUZZER, LOW);
    delay(50);
  }
}
