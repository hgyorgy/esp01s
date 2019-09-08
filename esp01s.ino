
/*
   ESP8266 Web server with Web Socket to control an RELAY.

   The web server keeps all clients' RELAY status up to date and any client may
   turn the RELAY on or off.

   For example, clientA connects and turns the RELAY on. This changes the word
   "RELAY" on the web page to the color red. When clientB connects, the word
   "RELAY" will be red since the server knows the RELAY is on.  When clientB turns
   the RELAY off, the word LED changes color to green on clientA and clientB web
   pages.

   References:

   https://github.com/Links2004/arduinoWebSockets
   
*/
/****************************************
  Include Libraries
****************************************/
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include <MQTTClient.h>           //https://github.com/256dpi/arduino-mqtt
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>

/****************************************
  Define Constants
****************************************/
/* WIFI Settings */
static const char wifiSSID[] = "WIFI";             // Your WifiSSID here
static const char wifiPassword[] = "WIFIPASS";  // Your wifi password here

/* MQTT Settings */
static const char mqttServer[] = "ip of mqtt server";  //your MQTT server IP or network name
const int mqttPort = 1883;  //1883 is the default port for MQTT. Change if necessary
static const char mqttDeviceID[] = "Device NAme";  //Unique device ID

// constants won't change. Used here to set a pin number:
const int ledPin = 2;// the number of the LED pin
// GPIO#0 is for Adafruit ESP8266 HUZZAH board. Your board RELAY might be on 13.
const int RELAYPIN = 0;

// Commands sent through Web Socket
const char RELAYON[] = "relayOn";
const char RELAYOFF[] = "relayOff";

/****************************************
  Define Variables
****************************************/
// Variables will change:

char* subscribeTopic = "SmartHouse/utilities/Relay01/command";      // Topic where send command
char* stateTopic = "SmartHouse/utilities/Relay01/state";            // Topic which listens for commands

static void writeLED(bool);

// Current RELAY status
bool RELAYStatus;
bool relayState;

int mqqtRetry = 0;
int wifiRetry = 0;

/****************************************
  Constructors
****************************************/
//Initialize Wifi
WiFiClient WiFiLan;

//Initialize MQTT
MQTTClient mqttClient;

//Webserver init
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

//MDNS Responder init
MDNSResponder mdns;


//Webpage
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name = "viewport" content = "width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0">
<title>ESP8266 WebSocket 1</title>
<style>
"body { background-color: #808080; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }"
</style>
<script>
var websock;
function start() {
  websock = new WebSocket('ws://' + window.location.hostname + ':81/');
  websock.onopen = function(evt) { console.log('websock open'); };
  websock.onclose = function(evt) { console.log('websock close'); };
  websock.onerror = function(evt) { console.log(evt); };
  websock.onmessage = function(evt) {
    console.log(evt);
    var e = document.getElementById('RELAYstatus');
    if (evt.data === 'relayOn') {
      e.style.color = 'red';
    }
    else if (evt.data === 'relayOff') {
      e.style.color = 'black';
    }
    else {
      console.log('unknown event');
    }
  };
}
function buttonclick(e) {
  websock.send(e.id);
}
</script>
</head>
<body onload="javascript:start();">
<h1>ESP8266 WebSocket Switch 1</h1>
<div id="RELAYstatus"><b>RELAY</b></div>
<button id="relayOn"  type="button" onclick="buttonclick(this);">On</button> 
<button id="relayOff" type="button" onclick="buttonclick(this);">Off</button>
</body>
</html>
)rawliteral";


/****************************************
Auxiliar Functions
****************************************/

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
  Serial.printf("webSocketEvent(%d, %d, ...)\r\n", num, type);
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\r\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
              
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        // Send the current RELAY status
        if (RELAYStatus) {
          webSocket.sendTXT(num, RELAYON, strlen(RELAYON));
        }
        else {
          webSocket.sendTXT(num, RELAYON, strlen(RELAYOFF));
        }
      }
      break;
    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\r\n", num, payload);

      if (strcmp(RELAYON, (const char *)payload) == 0) {
        writeRELAY(true);
        mqttClient.publish(stateTopic, "ON");
      }
      else if (strcmp(RELAYOFF, (const char *)payload) == 0) {
        writeRELAY(false);
        mqttClient.publish(stateTopic, "OFF");
      }
      else {
        Serial.println("Unknown command");
      }
      // send data to all connected clients
      webSocket.broadcastTXT(payload, length);
      break;
    case WStype_BIN:
      Serial.printf("[%u] get binary length: %u\r\n", num, length);
      hexdump(payload, length);

      // echo data back to browser
      webSocket.sendBIN(num, payload, length);
      break;
    default:
      Serial.printf("Invalid WStype [%d]\r\n", type);
      break;
  }
}

void handleRoot()
{
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

// Relay write
static void writeRELAY(bool relayOn)
{
  RELAYStatus = relayOn;
  // Note inverted logic for Adafruit HUZZAH board
  if (relayOn) {
    digitalWrite(RELAYPIN, 0);  //Switch off relay
    write_eeprom(true);   //save relay's state
  }
  else {
    digitalWrite(RELAYPIN, 1);  //Switch on relay
    write_eeprom(false);  //save relay's state
  }
}

//Write to eeprom
void write_eeprom(bool relayState) {
   EEPROM.write(0, relayState);    // Write state to EEPROM
   EEPROM.commit();
}

void messageReceived(String &subscribeTopic, String &payload) 
{
  String msgString = payload;
  
  if (msgString == "ON")
  {
    digitalWrite(RELAYPIN, 0); 
    write_eeprom(true);  //save relay's state             
    delay(1000);  
  }
  else if (msgString == "OFF")
  {
    digitalWrite(RELAYPIN, 1);   
    write_eeprom(false);  //save relay's state      
    delay(1000);  
  }
}

void mqtt_checkconnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Set the MQTT server to the server stated above ^
    if (mqttClient.connect(mqttDeviceID, true)) {
        Serial.println("connected");
        mqqtRetry = 0;
    } else {
      Serial.print("failed Retry.. ");
      Serial.println(".");
      mqqtRetry = mqqtRetry + 1;
      delay(1000);
    }
    if (mqqtRetry > 50) {
      Serial.println("Rebooting..");
      delay(1000);
      ESP.reset();
    }
  }
}

void wifi_checkconnect() {
   //Start Wifi connecting
  Serial.println( "Check WIFI connetion with " );
  Serial.print( wifiSSID ); 
  Serial.println(" ...");
  while ( WiFi.status() != WL_CONNECTED ) {
    if (WiFi.reconnect() == true ) {
      Serial.println ("Success!");
      wifiRetry=0;
      break;     
    } else {
      delay(1000);
      Serial.print(".");
      wifiRetry = wifiRetry + 1;
    }
    if (wifiRetry > 50) {
      Serial.println("Rebooting..");
      delay(1000);
      ESP.reset();
    }
  }
}
  
/****************************************
Main Functions
****************************************/
void setup()
{
  // Useful for debugging purposes
  Serial.begin(9600);

  
  EEPROM.begin(512);              // Begin eeprom to store on/off state
  relayState = EEPROM.read(0);

  pinMode(RELAYPIN, OUTPUT);
  writeRELAY(relayState);


  //Serial.setDebugOutput(true);

  //Serial.println();
  //Serial.println();
  //Serial.println();

  for(uint8_t t = 4; t > 0; t--) {
    Serial.printf("[SETUP] BOOT WAIT %d...\r\n", t);
    Serial.flush();
    delay(1000);
  }


  //Start Wifi connecting
  Serial.println( "Connecting to " );
  Serial.print( wifiSSID ); 
  Serial.println(" ...");
  
  /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default,
     would try to act as both a client and an access-point and could cause
     network-issues with your other WiFi-devices on your WiFi-network. */
  WiFi.mode(WIFI_STA);
  WiFi.begin( wifiSSID, wifiPassword);

  while ( WiFi.status() != WL_CONNECTED ) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("Success!");
  Serial.print("Connected to WIFI:");
  Serial.println( wifiSSID );
  Serial.print("Locale IP address: ");
  Serial.println(WiFi.localIP());

  //Connect to MQTT Server
  Serial.print("Connecting to MQTT server: ");
  Serial.print(mqttServer); 
  Serial.println(" ...");
  mqttClient.begin(mqttServer, mqttPort, WiFiLan);
  mqttClient.onMessage(messageReceived);

  //connect();
   while (!mqttClient.connect(mqttDeviceID)) 
   {
     Serial.print(".");
     delay(1000);
   }  
      
  Serial.println("Success!");
  Serial.print("Connected to MQTT Server: ");
  Serial.println(mqttServer);
  //Subscribe
  Serial.print("Subscribe to a topic:");
  Serial.println(subscribeTopic);
  mqttClient.subscribe(subscribeTopic);

  //MDNS responder start
  if (mdns.begin("espWebSock", WiFi.localIP())) {
    Serial.println("MDNS responder started");
    mdns.addService("http", "tcp", 80);
    //mdns.addService("ws", "tcp", 81);
  }
  else {
    Serial.println("MDNS.begin failed");
  }
  Serial.print("Connect to http://espWebSock.local or http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);

  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop()
{

  wifi_checkconnect();
  
  //MQTT loop
  mqttClient.loop();
  mqtt_checkconnect();
  
  delay(10);

//  if(!mqttClient.connected()) 
//  {
//    connect();
//  }
  
  webSocket.loop();
  server.handleClient();
}
