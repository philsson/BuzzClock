#include <Arduino.h>
#include <WiFi.h>
#include "driver/rtc_io.h"
#include "PubSubClient.h"

/****** Start of configuration ******/
#define uS_TO_S_FACTOR 1e6  /* Conversion factor for micro seconds to seconds */
#define S_TO_M_FACTOR 60
#define TIME_TO_SLEEP_W8_ALARM  60   /* Time ESP32 will go to sleep (in seconds) */
#define TIME_TO_SLEEP_DAILY 60*24
const bool isWakeOnTimerEnabled = false;
RTC_DATA_ATTR int bootCount = 0;

extern "C" int rom_phy_get_vdd33();

const char* ssid = "myssid";
const char* pass = "mypass";
const char* hostname = "BuzzClock";
const char* mqttServer = "mqtt.myserver.com"; // Not used
IPAddress mqttServerIP(192, 168, 1, 10);
const int mqttPort = 1883;
const char* mqttUser = "mymqttuser";
const char* mqttPass = "mymqttpass";
const char* mqttAlarmTopic = "BuzzClock";
const char* mqttBatTopic = "BuzzClock/Battery";
const gpio_num_t ISR_IO = GPIO_NUM_34;
/****** End of configuration ******/

void connectWifi() {
  WiFi.begin(ssid, pass);
  WiFi.setHostname(hostname);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    ++attempts;
    delay(1000);
    Serial.println("Establishing connection to WiFi...");
    if (attempts > 10)
    {
      Serial.println("Failed connecting to WiFi. Skipping...");
      return;
    }
  }
  Serial.println("Connected to network");
}

void publishMQTT(const char* topic, const char* msg) {
  WiFiClient espClient;
  PubSubClient client(espClient);
  client.setServer(mqttServerIP, mqttPort);
  
  /* while (!client.connected()) {} 
   * TODO: Maybe we should try to connect a few times 
   */
  if (client.connect(hostname, mqttUser, mqttPass)) {
    Serial.println("Publishing Mqtt");
    client.publish(topic, msg);
  }
  else {
    Serial.println("Failed to connect to MQTT Server");
  }
}

void publishMQTT(const char* msg) {
  publishMQTT(mqttAlarmTopic, msg);
}

float measureBat() {
  /* 1.1V is considered drained
   * Range [1.1, 1.5]
  */
  const float factor = 1689.2;
  float voltage = (float)rom_phy_get_vdd33()/factor;
  float percent = (voltage/2-1.1)/0.4*100;

  Serial.println("Voltage is " + String(voltage));
  Serial.println("Bat percentage is " + String(percent));
  if (percent > 100.0) {
    return 100.0;
  }
  return percent;
}

void runAtWakeUp()
{
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  connectWifi();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Woke up by Alarm");
      publishMQTT("buzz");
      /* We woke up by alarm. This will repeatedly wake up the 
       * controller until the alarm has "passed". Therefore
       * we wait before activating "Alarm Wake" again
       */
      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_W8_ALARM * uS_TO_S_FACTOR * S_TO_M_FACTOR);
      Serial.println("Alarm triggered. Setup ESP32 to sleep for " + String(TIME_TO_SLEEP_W8_ALARM) + " seconds");
      return;
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Woke up by timer.");
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
    case ESP_SLEEP_WAKEUP_ULP:
      Serial.println("Woke up by x");
      break;
    default:
      Serial.println("Wake not caused by sleep");
      publishMQTT("wake");
      break;
  }
  Serial.println("Activating Alarm Wake + Daily Wake");
  // We always want to activate "Alarm Wake" unless the alarm just rang
  esp_sleep_enable_ext0_wakeup(ISR_IO, LOW);
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_DAILY * uS_TO_S_FACTOR * S_TO_M_FACTOR);

  char batLevel[20];
  String(measureBat()).toCharArray(batLevel, 20);
  publishMQTT(mqttBatTopic, batLevel);
}

/* setup will run every time the controller boots
 * or wakes up from sleep 
 */
void setup() {
  ++bootCount;
  Serial.begin(115200);
  Serial.println("Boot nr " + String(bootCount));
  
  runAtWakeUp();

  /* Turn on LED */
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.println("Going to sleep now");
  delay(1000);
  Serial.flush(); 
  esp_deep_sleep_start();
}

/* Will never reach here as we go to sleep in setup 
 */
void loop() {}