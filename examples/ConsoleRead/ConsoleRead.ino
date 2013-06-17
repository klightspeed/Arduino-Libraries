/*
 Console.read() example:
 read data coming from bridge using the Console.read() function
 and store it in a string.
 
 To see the Console, pick your Yun's name and IP address in the Port menu
 then open the Port Monitor. You can also see it by opening a terminal window
 and typing 
 ssh root@ yourYunsName.local 'telnet localhost 6571'
 then pressing enter. When prompted for the password, enter it.
 
 created 13 Jun 2013
 by Angelo Scialabba 
 modified 16 June 2013
 by Tom Igoe
 
 This example code is in the public domain.
 */

#include <Console.h>

String name;

void setup() {
  //Initialize Console and wait for port to open:
  Bridge.begin();
  Console.begin(); 

  while (!Console){
    ; // wait for Console port to connect.
  }
  Console.println("Hi, what's your name?");
} 

void loop() {
  if (Console.available() > 0) {
    char thisChar = Console.read(); //read the next char received
    //look for the newline character, this is the last character in the string
    if (thisChar == '\n') {
      //print text with the name received
      Console.print("Hi ");
      Console.print(name);
      Console.println("! Nice to meet you!");
      Console.println();
      //Ask again for name and clear the old name
      Console.println("Hi, what's your name?");
      name = "";
    } 
    else {   //if the buffer is empty Cosole.read returns -1
      name += thisChar; //thisChar is int, treat him as char and add it to the name string
    }
  }
}


