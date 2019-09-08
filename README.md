# ESP01S Websocket and MQQT

Communication with an ESP8266 (ESP-01) through Websockets (you can swith on/off via http), and connect to an MQQT server.

What I improved?

WebSocket communication
Up until now, we've always used links (GET) and HTML forms (POST) to get data from the ESP, or to send data to it. This always resulted in a browser navigation action. There are many situations where you want to send data to the ESP without refreshing the page. 

One way to do this is by using AJAX and XMLHTTP requests. The disadvantage is that you have to establish a new TCP connection for every message you send. This adds a load of latency.
WebSocket is a technology that keeps the TCP connection open, so you can constantly send data back and forth between the ESP and the client, with low latency. And since it's TCP, you're sure that the packets will arrive intact. 

EEPROM Writing
 
Stores values read from input 0 into the EEPROM.
These values will stay in the EEPROM when the board is
Turned off or on and may the relay will switch back its original state after a reboot or a shutdown.

Fully MQTT controlled


The main goals are:
- Universal for relay boards with ESP8266 and ESP31s
- Remember last state during restart (reconnect, re-state..etc)
- Clearly MQTT controlled, ready to be controlled by Home Automation server, like OpenHAB
- What I've Just put it into the wall, behind the plug..

(This was my first project with arduino.)
