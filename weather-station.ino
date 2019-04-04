#include <ESPWiFi.h>
#include <ESPHTTPClient.h>
#include <JsonListener.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <sys/time.h>
#include <coredecls.h>
#include <SSD1306Wire.h>
#include <OLEDDisplayUi.h>
#include <Wire.h>
#include <OpenWeatherMapCurrent.h>
#include <OpenWeatherMapForecast.h>

#include "WeatherStationFonts.h"
#include "WeatherStationImages.h"

#define WIFI_TIMEOUT    120     // seconds
#define TZ              2       // (utc+) TZ in hours
#define DST_MN          0       // use 60mn for summer time in some countries

const int UPDATE_INTERVAL_SECS = 20 * 60; // 20min

// Display Settings
const int I2C_DISPLAY_ADDRESS = 0x3c;
#if defined(ESP8266)
const int SDA_PIN = D3;
const int SDC_PIN = D4;
#else
const int SDA_PIN = 5; //D3;
const int SDC_PIN = 4; //D4;
#endif

// OpenWeatherMap
String OPEN_WEATHER_MAP_APP_ID = "69d7fd68be2d89bd92614ee0fa5e1408";
// Go to https://openweathermap.org/find?q= and search for a location.
String OPEN_WEATHER_MAP_LOCATION_ID = "2925177"; // Freiburg
String OPEN_WEATHER_MAP_LANGUAGE = "en";
const uint8_t MAX_FORECASTS = 5;
const boolean IS_METRIC = true;

const String WDAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const String MONTH_NAMES[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// Initialize the oled display for address 0x3c
SSD1306Wire display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
OLEDDisplayUi ui(&display);

OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapCurrent currentWeatherClient;
OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
OpenWeatherMapForecast forecastClient;

#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

time_t now;

// Flag changed in the ticker function every 10 minutes
bool readyForWeatherUpdate = false;

String lastUpdate = "--";

long timeSinceLastWUpdate = 0;

// Prototypes
void drawProgress(OLEDDisplay* display, int percentage, String label);
void updateData(OLEDDisplay* display);
void drawDateTime(OLEDDisplay* display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentWeather(OLEDDisplay* display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecast(OLEDDisplay* display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecast2(OLEDDisplay* display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay* display, int x, int y, int dayIndex);
void drawHeaderOverlay(OLEDDisplay* display, OLEDDisplayUiState* state);
void setReadyForWeatherUpdate();
void drawOtaProgress(unsigned int, unsigned int);
void drawLoadingDots(unsigned int, String);

// This array keeps function pointers to all frames.
FrameCallback frames[] = {
  drawDateTime,
  drawCurrentWeather,
  drawForecast,
  drawForecast2,
};
int numberOfFrames = 4;

// This array keeps function pointers to all overlays.
OverlayCallback overlays[] = { drawHeaderOverlay };
int numberOfOverlays = 1;

void setup() {
  Serial.begin(115200);
  Serial.println();

  display.init();
  display.clear();
  display.display();

  //display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);

  String hostname("weather-");
  hostname += String(ESP.getChipId(), HEX);

  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);

  unsigned int counter = 0;

  if (WiFi.SSID() == "") {
    WiFi.beginSmartConfig();

    while (!WiFi.smartConfigDone()) {
      delay(500);
      Serial.print(".");
      drawLoadingDots(counter, "SmartConfig");
      counter++;
    }
  }

  WiFi.begin();

  counter = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    drawLoadingDots(counter, "Connecting to WiFi");
    counter++;

    if (counter == 2 * WIFI_TIMEOUT) {
      WiFi.disconnect(true);
      ESP.restart();
    }
  }

  // Get time from network time service.
  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");

  ui.setTargetFPS(30);
  ui.setActiveSymbol(activeSymbole);
  ui.setInactiveSymbol(inactiveSymbole);
  ui.setIndicatorPosition(BOTTOM);
  ui.setIndicatorDirection(LEFT_RIGHT);
  ui.setFrameAnimation(SLIDE_LEFT);
  ui.setFrames(frames, numberOfFrames);
  ui.setOverlays(overlays, numberOfOverlays);
  ui.init();

  Serial.println("");

  // Setup OTA
  ArduinoOTA.setHostname((const char*)hostname.c_str());
  ArduinoOTA.onProgress(drawOtaProgress);
  ArduinoOTA.begin();

  updateData(&display);
}

void loop() {

  if (millis() - timeSinceLastWUpdate > (1000L * UPDATE_INTERVAL_SECS)) {
    setReadyForWeatherUpdate();
    timeSinceLastWUpdate = millis();
  }

  if (readyForWeatherUpdate && ui.getUiState()->frameState == FIXED) {
    updateData(&display);
  }

  int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0) {
    // You can do some work here
    // Don't do stuff if you are below your time budget.
    ArduinoOTA.handle();

    delay(remainingTimeBudget);
  }
}

void updateData(OLEDDisplay* display) {
  drawProgress(display, 10, "Updating time...");
  drawProgress(display, 30, "Updating weather...");
  currentWeatherClient.setMetric(IS_METRIC);
  currentWeatherClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  currentWeatherClient.updateCurrentById(
    &currentWeather,
    OPEN_WEATHER_MAP_APP_ID,
    OPEN_WEATHER_MAP_LOCATION_ID
  );

  drawProgress(display, 50, "Updating forecasts...");
  forecastClient.setMetric(IS_METRIC);
  forecastClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  uint8_t allowedHours[] = {12};
  forecastClient.setAllowedHours(allowedHours, sizeof(allowedHours));
  forecastClient.updateForecastsById(
    forecasts,
    OPEN_WEATHER_MAP_APP_ID,
    OPEN_WEATHER_MAP_LOCATION_ID,
    MAX_FORECASTS
  );

  readyForWeatherUpdate = false;
  drawProgress(display, 100, "Done...");
  delay(1000);
}

void drawDateTime(OLEDDisplay* display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[16];


  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String date = WDAY_NAMES[timeInfo->tm_wday];

  sprintf_P(buff, PSTR("%s, %02d/%02d/%04d"), WDAY_NAMES[timeInfo->tm_wday].c_str(),
            timeInfo->tm_mday, timeInfo->tm_mon + 1, timeInfo->tm_year + 1900);
  display->drawString(64 + x, 5 + y, String(buff));
  display->setFont(ArialMT_Plain_24);

  sprintf_P(buff, PSTR("%02d:%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
  display->drawString(64 + x, 15 + y, String(buff));
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawCurrentWeather(OLEDDisplay* display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, 38 + y, currentWeather.description);

  display->setFont(ArialMT_Plain_24);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(58 + x, 5 + y, String(currentWeather.temp, 1) + "°C");

  display->setFont(Meteocons_Plain_36);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(32 + x, 0 + y, currentWeather.iconMeteoCon);
}

void drawForecastDetails(OLEDDisplay* display, int x, int y, int dayIndex) {
  time_t observationTimestamp = forecasts[dayIndex].observationTime;
  struct tm* timeInfo;
  timeInfo = localtime(&observationTimestamp);

  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y, WDAY_NAMES[timeInfo->tm_wday]);

  display->setFont(Meteocons_Plain_21);
  display->drawString(x + 20, y + 12, forecasts[dayIndex].iconMeteoCon);

  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y + 34, String(forecasts[dayIndex].temp, 0)  + "°C");
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawForecast(OLEDDisplay* display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  drawForecastDetails(display, x, y, 0);
  drawForecastDetails(display, x + 44, y, 1);
  drawForecastDetails(display, x + 88, y, 2);
}

void drawForecast2(OLEDDisplay* display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  drawForecastDetails(display, x + 22, y, 3);
  drawForecastDetails(display, x + 66, y, 4);
}

void drawHeaderOverlay(OLEDDisplay* display, OLEDDisplayUiState* state) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[14];
  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);

  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);

  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 54, String(buff));

  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->drawString(128, 54, String(currentWeather.temp, 1) + "°C");

  display->drawHorizontalLine(0, 52, 128);
}

void setReadyForWeatherUpdate() {
  Serial.println("Setting readyForUpdate to true");
  readyForWeatherUpdate = true;
}


void drawProgress(OLEDDisplay* display, int percentage, String label) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 12, percentage);
  display->display();
}

void drawOtaProgress(unsigned int progress, unsigned int total) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 10, "OTA Update");
  display.drawProgressBar(2, 28, 124, 12, progress / (total / 100));
  display.display();
}

void drawLoadingDots(unsigned int counter, String message) {
  display.clear();
  display.drawString(64, 10, message);
  display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbole : inactiveSymbole);
  display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbole : inactiveSymbole);
  display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbole : inactiveSymbole);
  display.display();
}
