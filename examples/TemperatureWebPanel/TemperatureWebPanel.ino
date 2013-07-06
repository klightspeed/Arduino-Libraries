/*
  Temperature web interface
 
 This example shows how to serve data from an analog input  
via the Arduino Yún's built-in webserver using the Bridge library.
 	
 The circuit:
 * TMP36 temperature sensor on analog pin A1
 * SD card attached to SD card slot of the Arduino Yún
 
 Prepare your SD card with an empty folder in the SD root 
 named "arduino" and a subfolder of that named "www". 
 This will ensure that the Yún will create a link 
 to the SD to the "/mnt/sd" path.
 
 In this sketch folder is a basic webpage and a copy of zepto.js, a 
 minimized version of jQuery.  When you upload your sketch, these files
 will be placed in the /arduino/www/TemperatureWebPanel folder on your SD card.
 
 You can then go to http://arduino.local/sd/TemperatureWebPanel
 to see the output of this sketch.
 
 You can remove the SD card while the Linux and the 
 sketch are running but be careful not to remove it while
 the system is writing to it.
 
 created  6 July 2013
 by Tom Igoe

 
 This example code is in the public domain.
 	 
 */
#include <Bridge.h>
#include <YunServer.h>

// Listen on default port 5555, the webserver on the Yun
// will forward there all the HTTP requests for us.
YunServer server;

void setup() {
  Serial.begin(9600);

  // Bridge startup
  pinMode(13,OUTPUT);
  digitalWrite(13, LOW);
  Bridge.begin();
  digitalWrite(13, HIGH);

  // using A0 and A2 as vcc and gnd for the TMP36 sensor:
  pinMode(A0, OUTPUT);
  pinMode(A2, OUTPUT);
  digitalWrite(A0, HIGH);
  digitalWrite(A2, LOW);

  // Listen for incoming connection only from localhost
  // (no one from the external network could connect)
  server.listenOnLocalhost();
  server.begin();
}

void loop() {
  // Get clients coming from server
  YunClient client = server.accept();

  // There is a new client?
  if (client) {
    // read the command
    String command = client.readString();
    command.trim();        //kill whitespace
    Serial.println(command);
    // is "temperature" command?
    if (command == "temperature") {
      int sensorValue = analogRead(A1);
      // convert the reading to millivolts:
      float voltage = sensorValue *  (5000/ 1024); 
      // convert the millivolts to temperature celsius:
      float temperature = (voltage - 500)/10;
      // print the temperature:
      client.print("Current temperature: ");
      client.print(temperature);
      client.print(" degrees C");

    }

    // Close connection and free resources.
    client.stop();
  }

  delay(50); // Poll every 50ms
}

