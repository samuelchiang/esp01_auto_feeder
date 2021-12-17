#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <FS.h>          // this needs to be first, or it all crashes and burns...
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson
#include <FS.h>

const int feeder_pin = 2;
//define your default values here, if there are different values in config.json, they are overwritten.
char mqttServer[40] = "";  // MQTT伺服器位址
char mqttPort[6]  = "1883";
char mqttUserName[32] = "";  // 使用者名稱
char mqttPwd[32] = "";  // MQTT密碼
char DeviceId[32] = "auto_feeder";  // Device ID

char EventTopic[64];
char CommandTopic[64];

//flag for saving data
bool shouldSaveConfig = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

//--------------- Wifi
void setup_wifi() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);

  //WifiManager 還提供一個特別的功能，就是可以在 Config Portal 多設定其他的參數，
  // 這等於是提供了參數的設定介面，可以避免將參數寫死在程式碼內。流程如下所述
  //     1. 開機時，讀取 SPIFFS, 取得 config.json 檔案，讀入各參數，若沒有，則採用預設值
  //     2. 若 ESP 32 進入 AP 模式，則在 Config Portal 可設定參數
  //     3. 設定完之後，將參數寫入 SPIFFS 下的 config.json
  //
  // setup custom parameters
  //
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqttServer("mqttServer", "mqtt server", mqttServer, 40);
  WiFiManagerParameter custom_mqttPort("mqttPort", "mqtt port", mqttPort, 6);
  WiFiManagerParameter custom_mqttUserName("mqttUserName", "mqtt user name", mqttUserName, 32);
  WiFiManagerParameter custom_mqttPwd("mqttPwd", "mqtt password", mqttPwd, 32);
  WiFiManagerParameter custom_DeviceId("DeviceId", "Device ID", DeviceId, 32);

  //wm.resetSettings();

  //add all your parameters here
  wm.addParameter(&custom_mqttServer);
  wm.addParameter(&custom_mqttPort);
  wm.addParameter(&custom_mqttUserName);
  wm.addParameter(&custom_mqttPwd);
  wm.addParameter(&custom_DeviceId);

  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  //res = wm.autoConnect("SmartPlug"); // anonymous ap
  //若重啟後，還是連不上家裡的  Wifi AP, ESP32 就會進入設定的 AP 模式，必須將這個模式設定三分鐘 timeout 後重啟，
  //萬一家裡的 Wifi AP 恢復正常，ESP32 就可以直接連線。這裡的三分鐘 timeout, 經過測試，只要有在 Config Portal 頁面操作，
  //每次都會重置三分鐘timeout，所以在設定時，不需緊張。
  wm.setConfigPortalTimeout(180);//seconds
  res = wm.autoConnect(DeviceId, "1qaz2wsx"); // password protected ap
  if (!res) {
    Serial.println("Failed to connect");
    ESP.restart();
  }
  else {
    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
  }

  //read updated parameters
  strcpy(mqttServer, custom_mqttServer.getValue());
  strcpy(mqttPort, custom_mqttPort.getValue());
  strcpy(mqttUserName, custom_mqttUserName.getValue());
  strcpy(mqttPwd, custom_mqttPwd.getValue());
  strcpy(DeviceId, custom_DeviceId.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonDocument doc(1024);

    doc["mqttServer"]   = mqttServer;
    doc["mqttPort"]     = mqttPort;
    doc["mqttUserName"] = mqttUserName;
    doc["mqttPwd"]      = mqttPwd;
    doc["DeviceId"]     = DeviceId;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    serializeJsonPretty(doc, Serial);
    serializeJson(doc, configFile);
    configFile.close();
    //end save
    shouldSaveConfig = false;
  }
}


//--------------- MQTT
void mqttReconnect() {
  int countdown = 5;
  while (!mqttClient.connected()) {
    if (mqttClient.connect(DeviceId, mqttUserName, mqttPwd)) {
      Serial.println("MQTT connected");
      mqttClient.subscribe(CommandTopic);
      mqttClient.setCallback(mqttCallback);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);  // 等5秒之後再重試'
      //設置 timeout, 過了 25 秒仍無法連線, 就重啟 EPS32
      countdown--;
      if (countdown == 0) {
        Serial.println("Failed to reconnect");
        ESP.restart();
      }
    }
  }
}

void mqtt_publish(const char* topic, String str) {
  // 宣告字元陣列
  byte arrSize = str.length() + 1;
  char msg[arrSize];
  Serial.print("Publish topic: ");
  Serial.print(topic);
  Serial.print(" message: ");
  Serial.print(str);
  Serial.print(" arrSize: ");
  Serial.println(arrSize);
  str.toCharArray(msg, arrSize); // 把String字串轉換成字元陣列格式
  if (!mqttClient.publish(topic, msg)) {
    Serial.println("Faliure to publish, maybe you should check the message size: MQTT_MAX_PACKET_SIZE 128");       // 發布MQTT主題與訊息
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // handle message arrived
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  if (strcmp(topic, CommandTopic) == 0 && memcmp(payload, "run", 3) == 0) {
    Serial.println("run auto feeder");
    digitalWrite(feeder_pin, HIGH);
    delay(1000);
    digitalWrite(feeder_pin, LOW);
    return;
  }

//  if (strcmp(topic, CommandTopic) == 0 && memcmp(payload, "run", 3) == 0) {
//    Serial.println("run auto feeder");
//    digitalWrite(feeder_pin, HIGH);
//    return;
//  }if (strcmp(topic, CommandTopic) == 0 && memcmp(payload, "stop", 4) == 0) {
//    Serial.println("stop auto feeder");
//    digitalWrite(feeder_pin, LOW);
//    return;
//  }

}


void report_status() {
  static unsigned long samplingTime = millis();
  unsigned long samplingInterval = 10000; //ms

  if (millis() - samplingTime > samplingInterval)
  {
    Serial.println();

    // 組合MQTT訊息；
    String msgStr = "{ \"feeder_status\":1 }";
    mqtt_publish(EventTopic, msgStr);
    msgStr = "";
    samplingTime = millis();
  }
}

void setup_topic() {
  sprintf(&EventTopic[0], "status/%s/", DeviceId); // "status/auto_feeder/";
  sprintf(&CommandTopic[0], "cmd/%s/", DeviceId); //  "cmd/auto_feeder/";
}

void setup_spiffs() {
  //clean FS, for testing
  // SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument doc(1024);

        deserializeJson(doc, buf.get(), DeserializationOption::NestingLimit(20));
        serializeJsonPretty(doc, Serial);

        if (!doc.isNull()) {
          Serial.println("\nparsed json");

          if (doc.containsKey("mqttServer")) {
            strcpy(mqttServer, doc["mqttServer"]);
          }
          if (doc.containsKey("mqttPort")) {
            strcpy(mqttPort, doc["mqttPort"]);
          }
          if (doc.containsKey("mqttUserName")) {
            strcpy(mqttUserName, doc["mqttUserName"]);
          }
          if (doc.containsKey("mqttPwd")) {
            strcpy(mqttPwd, doc["mqttPwd"]);
          }
          if (doc.containsKey("DeviceId")) {
            strcpy(DeviceId, doc["DeviceId"]);
          }

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
}

void setup() {
  pinMode(feeder_pin, OUTPUT);
  digitalWrite(feeder_pin, LOW);
  Serial.begin(115200);
  setup_spiffs(); // Read parameter from FS, if no data, use default
  setup_wifi(); // If running on AP mode, get the paramters from config portal
  setup_topic(); // Configure Topic with Device ID
  mqttClient.setServer(mqttServer, atoi(mqttPort));
}

void loop() {
  if (!mqttClient.connected()) {
    mqttReconnect();
  }
  mqttClient.loop();


  report_status();
}
