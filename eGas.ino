#include "secrets.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <WiFi.h>

AsyncWebServer server(80);                                    // Create AsyncWebServer object on port 80

char publishTopic[1024];                                      // adjust this based on your expected file size

#define AWS_IOT_SUBSCRIBE_TOPIC "eGas/sub"  
#define LED_AWS 2
#define LED_WIFI 5

WiFiClientSecure net = WiFiClientSecure();
PubSubClient client(net);

// Search for parameter in HTTP POST request
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
const char* PARAM_INPUT_3 = "connectID";

//Variables to save values from HTML form
String ssid;
String pass;
String connectID;

// File paths to save input values permanently
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* connectIDPath = "/connectID.txt";

IPAddress localIP;
IPAddress localGateway;                                         // Set your Gateway IP address
IPAddress subnet(255, 255, 0, 0);                               // Set your subnet

// Timer variables
unsigned long previousMillis = 0;
unsigned long previousTiming = 0;
const long interval = 10000;                                    // interval to wait for Wi-Fi connection (milliseconds)

bool wifiFlag = false;
bool awsFlag = false;
bool control = false;
uint32_t  track = 0;
uint32_t count = 0;

// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin(true)) Serial.println("An error has occurred while mounting SPIFFS");
  Serial.println("SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path);
  if(!file || file.isDirectory()){
    Serial.println("- failed to open file for reading");
    return String();
  }
  String fileContent;
  while(file.available()){
    fileContent = file.readStringUntil('\n');
    break;     
  }
  return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- frite failed");
  }
}

// Initialize WiFi
bool initWiFi() {
  if(ssid=="" || connectID==""){
    Serial.println("Undefined SSID or connectID address.");
    return false;
  }
  WiFi.mode(WIFI_STA);
  localIP.fromString(connectID.c_str());
  
  if (!WiFi.config(localIP, localGateway, subnet)){
    Serial.println("STA Failed to configure");
    return false;
  }
  
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;
  
  Serial.println("Connecting to WIFI.");
  
  while(WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval){
      WiFi.begin(ssid.c_str(), pass.c_str()); 
      digitalWrite(LED_WIFI, LOW);
      count +=1;
      previousMillis = millis();  
      Serial.print("-> Trying to reconnect.");   
      if(count == 5){
        Serial.println("Failed to connect.");
        count = 0;
        return false;
      }
    }
  }
  Serial.println(WiFi.localIP());
  wifiFlag = true;
  return wifiFlag;
}


void connectAWS(){
  // Configure WiFiClientSecure to use the AWS IoT device credentials
  awsFlag = false;
  digitalWrite(LED_AWS, LOW);
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);
  client.setServer(AWS_IOT_ENDPOINT, 8883);                                 // Connect to the MQTT broker on the AWS endpoint we defined earlier
  client.setCallback(messageHandler);                                       // Create a message handler
  Serial.println("Connecting to AWS IOT");
   
  while(!client.connect(THINGNAME)){
    Serial.print(".");
    delay(100);
  }
  
  while(!client.connected()){
    Serial.println("AWS IoT Timeout -> Retrying");
    previousTiming += 1;
    if(previousTiming < 3){
      while(!client.connect(THINGNAME)){
        Serial.print(".");
        delay(100);
      }
    }
    if(previousTiming >= 3){
      Serial.println("Device will restart after 2 seconds");
      delay(2000);
      ESP.restart();
      // return;
    }
  }
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);                                // Subscribe to a topic
  Serial.println("AWS IoT Connected!");
  awsFlag = true;
}

void publishMessage(){
  StaticJsonDocument<200> doc;
  doc["time"] = millis();
  doc["sensor_a0"] = analogRead(35);
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);                                           // print to client
  client.publish(publishTopic, jsonBuffer);
}

void messageHandler(char* topic, byte* payload, unsigned int length) {
  Serial.print("incoming: ");
  Serial.println(topic);
  StaticJsonDocument<200> doc;
  deserializeJson(doc, payload);
  const char* message = doc["message"];
  Serial.println(message);
}

void setup() {
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_AWS, OUTPUT);
  digitalWrite(LED_WIFI, LOW);
  digitalWrite(LED_AWS, LOW);
 
  Serial.begin(115200);

  initSPIFFS();

  // Load values saved in SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  connectID = readFile(SPIFFS, connectIDPath);
  
  // Open the connectID file for reading, for the purpose of formating it corectly as a publish topic
  File file = SPIFFS.open(connectIDPath, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  // Read the file content
  size_t bytes_read = file.readBytes(publishTopic, sizeof(publishTopic) - 1);
  publishTopic[bytes_read] = '\0';                                                // Add null terminator manually
  file.close();                                                                   // Close the file
 
  Serial.println(ssid);
  Serial.println(pass);
  Serial.println(connectID);
  Serial.println("Publish Topic:");
  Serial.println(publishTopic);

  if(initWiFi()){
    connectAWS();
  }else {
    Serial.println("Setting AP (Access Point)");                                  // Connect to Wi-Fi network with SSID and password
    WiFi.softAP("Freedisity", NULL);                                              // NULL sets an open Access Point

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP); 

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/index.html", "text/html");
    });
    
    server.serveStatic("/", SPIFFS, "/");
    
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      int params = request->params();
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
          // HTTP POST ssid value
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Write file to save value
            writeFile(SPIFFS, ssidPath, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Write file to save value
            writeFile(SPIFFS, passPath, pass.c_str());
          }
          // HTTP POST ip value
          if (p->name() == PARAM_INPUT_3) {
            connectID = p->value().c_str();
            Serial.print("connectID Address set to: ");
            Serial.println(connectID);
            // Write file to save value
            writeFile(SPIFFS, connectIDPath, connectID.c_str());
          }
        }
      }
      request->send(200, "text/html", handShake());
      delay(3000);
      ESP.restart();
    });
    server.begin();
  }
}

String handShake(){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>Success!</title>\n";
  ptr +="<style>html { font-family: Helvetica;}\n";
  ptr +="body{display: flex;  justify-content: center; align-items: center; margin: 0px auto;background-color: #fff; margin-top: 50px;}\n";
  ptr +="div{display: flex;  flex-direction: column; justify-content: center; align-items: center; margin: 0px auto;background-color: width:100%; height: 200px; #eee; margin-top: 50px;}\n";
  ptr +="h1{color: green; font-size:20px;}\n";
  ptr +="h2{color: #333; font-size:18px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<div>\n";
  ptr +="<h1>Device successfully configured!</h1>\n"; 
  ptr +="<h2>You can now return to the Mobile App</h2>\n"; 
  ptr +="</div>\n";
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

void loop() {
  publishMessage();
  client.loop();  delay(100);     // This at the beginning of the loop improved stability and was recommended
  LedBlink();
  if(awsFlag){
    if (!client.connected()){
      connectAWS();
    }
  }  delay(500);
}

void LedBlink(){
  if(wifiFlag) digitalWrite(LED_WIFI, HIGH);
  else digitalWrite(LED_WIFI, LOW);
  if(awsFlag){
    uint32_t curr = millis();
    if(!control){
      track = curr;
      control = true;
    }
    if(curr - track >= 2000){
      digitalWrite(LED_AWS, HIGH);
      delay(100);
      digitalWrite(LED_AWS, LOW);
      control = false;
    }
  }
}
