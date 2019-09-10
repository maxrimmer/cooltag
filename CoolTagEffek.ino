#include <SPI.h>
#include <MFRC522.h>

#define RST_PIN         0           // Configurable, see typical pin layout above
#define SS_PIN          2          // Configurable, see typical pin layout above

MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.

MFRC522::MIFARE_Key key;

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>

#include "pitches.h";

// notes in the melody:
int melody[] = {
  NOTE_D6, NOTE_D5, NOTE_A5, NOTE_G5, NOTE_D5, NOTE_D6, NOTE_A6, NOTE_A6
};

// note durations: 4 = quarter note, 8 = eighth note, etc.:
int noteDurations[] = {
  4, 8, 8, 4, 4, 4, 4, 4
};

int speakerPin = 15;




/* Set these to your desired softAP credentials. They are not configurable at runtime */
#ifndef APSSID
#define APSSID "CoolTag"
#define APPSK  "CoolTagBeta"
#endif

const char *softAP_ssid = APSSID;
const char *softAP_password = APPSK;

/* hostname for mDNS. Should work at least on windows. Try http://esp8266.local */
const char *myHostname = "esp8266";

/* Don't set this wifi credentials. They are configurated at runtime and stored on EEPROM */
char ssid[32] = "";
char password[32] = "";

// DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;

// Web server
ESP8266WebServer server(80);

/* Soft AP network parameters */
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);


/** Should I connect to WLAN asap? */
boolean connect;

/** Last time I tried to connect to WLAN */
unsigned long lastConnectTry = 0;

/** Current WLAN status */
unsigned int status = WL_IDLE_STATUS;


String link;

const int stb = 16;
const int clk = 5;
const int dio = 4;

void sendCommand(byte value) {//-- funktion som kan sende en enkelt command
  digitalWrite(stb, LOW);
  shiftOut(dio, clk, LSBFIRST, value);
  digitalWrite(stb, HIGH);
}

void writeScreen(int location, byte value) {
  byte loc = 0x00;
  switch(location) {
    case 1:
      loc = 0xC0;
      break;
    case 2:
      loc = 0xC2;
      break;
    case 3:
      loc = 0xC4;
      break;
    case 4:
      loc = 0xC6;
      break;
    case 5:
      loc = 0xC8;
      break;
    case 6:
      loc = 0xCA;
      break;
    case 7:
      loc = 0xCC;
      break;
    case 8:
      loc = 0xCE;
      break;
    default:
      loc = 0xC0; 
      break;    
  }

  digitalWrite(stb, LOW);
  shiftOut(dio, clk, LSBFIRST, loc);
  shiftOut(dio, clk, LSBFIRST, value);
  digitalWrite(stb, HIGH);
  
}

void resetScreen() { //----- reset funktion, slukker alle dioder og segmenter
  sendCommand(0x40);
  digitalWrite(stb, LOW);
  shiftOut(dio, clk, LSBFIRST, 0xc0);
  for (byte i = 0; i < 16; i++)
  {
    shiftOut(dio, clk, LSBFIRST, 0x00);
  }
  digitalWrite(stb, HIGH);
}

byte twoDigitToSegment3(int num) {
  int numRest = num % 1000;
  int firstDigit = numRest;  // 462
  numRest = firstDigit % 100; // 62
  int thirdDigit = numRest / 10;


  return intToSegment(thirdDigit);
}

byte twoDigitToSegment4(int num) {
  int numRest = num % 10;

  return intToSegment(numRest);
}

byte twoDigitToSegment2(int num) {
  int numRest = num % 1000;
  int secondDigit = numRest / 100; // 462


  return intToSegment(secondDigit);
}

byte twoDigitToSegment1(int num) {
  int numRest = num % 1000;
  int firstDigit = (num - numRest) / 1000;

  return intToSegment(firstDigit);
}


byte intToSegment(int num) {
  switch (num) {
    case 0:
      return 0x3F;
      break;
    case 1:
      return 0x06;
      break;
    case 2:
      return 0x5B;
      break;
    case 3:
      return 0x4F;
      break;
    case 4:
      return 0x66;
      break;
    case 5:
      return 0x6D;
      break;
    case 6:
      return 0x7C;
      break;
    case 7:
      return 0x07;
      break;
    case 8:
      return 0x7F;
      break;
    case 9:
      return 0x67;
      break;
    case 10:
      return 0x40;
      break;
    case 11:
      return 0x39;
      break;
    case 12:
      return 0x5C;
      break;
    case 13:
      return 0x54;
      break;

  }
}


byte readButtons(void) { //---- læser et keyPad matrix mønster og returnerer en
  //---- byte med det samlede resultat

  byte buttons = 0;
  digitalWrite(stb, LOW);
  shiftOut(dio, clk, LSBFIRST, 0x42);
  pinMode(dio, INPUT);
  for (byte i = 0; i < 4; i++)
  {
    byte v = shiftIn(dio, clk, LSBFIRST) << i;
    buttons |= v;
  }
  pinMode(dio, OUTPUT);
  digitalWrite(stb, HIGH);
  return buttons;
}


void dump_byte_array(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}

void setup() {

  playMelody();

  SPI.begin();        // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522 card

  // Prepare the key (used both as key A and as key B)
  // using FFFFFFFFFFFFh which is the default at chip delivery from the factory
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  Serial.println(F("Scan a MIFARE Classic PICC to demonstrate read and write."));
  Serial.print(F("Using key (for A and B):"));
  dump_byte_array(key.keyByte, MFRC522::MF_KEY_SIZE);
  Serial.println();

  Serial.println(F("BEWARE: Data will be written to the PICC, in sector #1"));

  pinMode(stb, OUTPUT);
  pinMode(clk, OUTPUT);
  pinMode(dio, OUTPUT);


  // reset skærmen
  sendCommand(0x89);
  resetScreen();
  sendCommand(0x40);

  // Sæt lysstyrken op
  sendCommand(0x8F);


  // Vis Conn
  writeScreen(1, intToSegment(11));
  writeScreen(2, intToSegment(12));
  writeScreen(3, intToSegment(13));
  writeScreen(4, intToSegment(13));


  Serial.begin(9600);
  Serial.println();
  Serial.println("Configuring access point...");
  /* You can remove the password parameter if you want the AP to be open. */
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(softAP_ssid, softAP_password);
  delay(500); // Without delay I've seen the IP address blank
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  /* Setup the DNS server redirecting all the domains to the apIP */
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  /* Setup web pages: root, wifi config pages, SO captive portal detectors and not found. */
  server.on("/", handleRoot);
  server.on("/wifi", handleWifi);
  server.on("/wifisave", handleWifiSave);
  server.on("/generate_204", handleRoot);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
  server.on("/fwlink", handleRoot);  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  server.onNotFound(handleNotFound);
  server.begin(); // Web server start
  Serial.println("HTTP server started");
  loadCredentials(); // Load WLAN credentials from network
  connect = strlen(ssid) > 0; // Request WLAN connect if there is a SSID



}

void connectWifi() {
  Serial.println("Connecting as wifi client...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  int connRes = WiFi.waitForConnectResult();
  Serial.print("connRes: ");
  Serial.println(connRes);
}

void playMelody() {
  
        for (int thisNote = 0; thisNote < 8; thisNote++) {
      
          // to calculate the note duration, take one second divided by the note type.
          //e.g. quarter note = 1000 / 4, eighth note = 1000/8, etc.
          int noteDuration = 1000 / noteDurations[thisNote];
          tone(speakerPin, melody[thisNote], noteDuration);
      
          // to distinguish the notes, set a minimum time between them.
          // the note's duration + 30% seems to work well:
          int pauseBetweenNotes = noteDuration * 1.30;
          delay(pauseBetweenNotes);
          // stop the tone playing:
          noTone(speakerPin);
        }
}

void writeFreeze(bool freeze) {
                  if (freeze) {

                  writeScreen(5, intToSegment(10));
                  writeScreen(6, intToSegment(1));
                  writeScreen(7, intToSegment(8));
                  writeScreen(8, 0x63);
                  
                                    
                } else {

                  writeScreen(5, 0x00);
                  writeScreen(6, 0x00);
                  writeScreen(7, intToSegment(5));
                  writeScreen(8, 0x63);
                                
                }
}

void loop() {
  if (connect) {
    Serial.println("Connect requested");
    connect = false;
    connectWifi();
    lastConnectTry = millis();
  }
  {
    unsigned int s = WiFi.status();
    if (s == 0 && millis() > (lastConnectTry + 60000)) {
      /* If WLAN disconnected and idle try to connect */
      /* Don't set retry time too low as retry interfere the softAP operation */
      connect = true;
    }
    if (status != s) { // WLAN status change
      Serial.print("Status: ");
      Serial.println(s);
      status = s;
      if (s == WL_CONNECTED) {
        /* Just connected to WLAN */
        Serial.println("");
        Serial.print("Connected to ");
        Serial.println(ssid);
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        // Setup MDNS responder
        if (!MDNS.begin(myHostname)) {
          Serial.println("Error setting up MDNS responder!");
        } else {
          Serial.println("mDNS responder started");
          // Add service to MDNS-SD
          MDNS.addService("http", "tcp", 80);
        }

        HTTPClient http;


        link = "http://anderledes.net/ddu19/ping.php?deviceid=123456";

        
        http.begin(link);  //Specify request destination
        int httpCode = http.GET();                                                                  //Send the request
        Serial.println(httpCode);

        String payload = http.getString();   //Get the request response payload
        Serial.println(payload);                     //Print the response payload
        Serial.println("Prøver vi?");

                // reset skærmen
        sendCommand(0x89);
        resetScreen();
        sendCommand(0x40);

        // Sæt lysstyrken op
        sendCommand(0x8F);

        byte keyMatrix = readButtons();

        if(payload != "paired;") {
          if (keyMatrix != 1) {
            delay(100);

            // Skriv ----
            for (int i = 1; i < 5; i++) {
             writeScreen(i, intToSegment(10));
            }
  
            while (keyMatrix != 1) {
              keyMatrix = readButtons();
              delay(100);
            }
          }
  
  
          link = "http://anderledes.net/ddu19/newdevice.php?deviceid=123456";
  
          http.begin(link);  //Specify request destination
          int httpCode = http.GET();                                                                  //Send the request
  
          String payload = http.getString();   //Get the request response payload
          Serial.println(payload);                     //Print the response payload
          int code = payload.toInt();
          //Serial.println("Kode: " + code);
  
  
          delay(1000);
  
          digitalWrite(stb, HIGH);
          delay(1000);
          digitalWrite(stb, LOW);
          digitalWrite(stb, HIGH);
          delay(1000);
  
  
  
  
          // Skriv kode ud:
          sendCommand(0x40);

          writeScreen(1, twoDigitToSegment1(code));
          writeScreen(2, twoDigitToSegment2(code));
          writeScreen(3, twoDigitToSegment3(code));
          writeScreen(4, twoDigitToSegment4(code));

  
          delay(5000);
  
          payload = "";
  
          while (payload != "paired;") {
            link = "http://anderledes.net/ddu19/ping.php?deviceid=123456";
  
            http.begin(link);  //Specify request destination
            int httpCode = http.GET();                                                                  //Send the request
  
            payload = http.getString();   //Get the request response payload
            Serial.println(payload);
            delay(5000);
          }
        }

        playMelody();
        

        

        while (1) {

          delay(100);

          byte keyMatrix = readButtons();


          for(int i = 1; i < 9; i++) {
            writeScreen(i, intToSegment(10));
          }
          
          if(keyMatrix == 2) {
            delay(1000);
            int qty = 1;
            bool freeze = false;
            byte KeyMatrix = readButtons();
            while((KeyMatrix == 0x02) || (KeyMatrix == 0x04) || (KeyMatrix == 0x08) || (KeyMatrix == 0x10) || (KeyMatrix == 0x20)) {
              delay(100);
              KeyMatrix = readButtons();
              
            }
            keyMatrix = readButtons();
            while(keyMatrix != 2) {


              if(qty > 10) {
                writeScreen(1, twoDigitToSegment3(qty));
                writeScreen(2, twoDigitToSegment4(qty));
                for(int i = 3; i < 9; i++) {
                  writeScreen(i, 0x00);
                }


                writeFreeze(freeze);
              } else {

                writeScreen(1, twoDigitToSegment4(qty));
                for(int i = 2; i < 9; i++) {
                  writeScreen(i, 0x00);
                }

                writeFreeze(freeze);
              }
              delay(500);

              keyMatrix = readButtons();
              switch (keyMatrix) {
                case 2:
                  break;
                case 8:
                  qty++;
                  break;
                case 16:
                  qty +=5;
                  break;
                case 32:
                  qty--;
                  break;
                case 64:
                  qty -= 5;
                  break; 
                case 128:
                  freeze = !freeze;
                  break;
  
              }
            }

            

              
              
            

            for(byte i = 0xC1; i <= 0xCF; i = i+2) {
              digitalWrite(stb, LOW);
              shiftOut(dio, clk, LSBFIRST, i);
              shiftOut(dio, clk, LSBFIRST, 0xFF);
              digitalWrite(stb, HIGH);
              delay(50);
             }
                        
            if (mfrc522.PICC_IsNewCardPresent()) {
              if (mfrc522.PICC_ReadCardSerial()) {
                // Show some details of the PICC (that is: the tag/card)
            Serial.print(F("Card UID:"));
            dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
            Serial.println();
            Serial.print(F("PICC type: "));
            MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
            Serial.println(mfrc522.PICC_GetTypeName(piccType));
  
            // Check for compatibility
            if (    piccType != MFRC522::PICC_TYPE_MIFARE_MINI
                    &&  piccType != MFRC522::PICC_TYPE_MIFARE_1K
                    &&  piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
              Serial.println(F("This sample only works with MIFARE Classic cards."));
              return;
            }
  
            byte sector         = 15;
            byte blockAddr      = 60;
            byte trailerBlock   = 63;
            MFRC522::StatusCode status;
            byte buffer[18];
            byte size = sizeof(buffer);
  
  
            // Authenticate using key A
            Serial.println(F("Authenticating using key A..."));
            status = (MFRC522::StatusCode) mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
            if (status != MFRC522::STATUS_OK) {
              Serial.print(F("PCD_Authenticate() failed: "));
              Serial.println(mfrc522.GetStatusCodeName(status));
              return;
            }
  
            // Show the whole sector as it currently is
            Serial.println(F("Current data in sector:"));
            mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);
            Serial.println();
  
  
            // Read data from the block
            Serial.print(F("Reading data from block ")); Serial.print(blockAddr);
            Serial.println(F(" ..."));
            status = (MFRC522::StatusCode) mfrc522.MIFARE_Read(blockAddr, buffer, &size);
            if (status != MFRC522::STATUS_OK) {
              Serial.print(F("MIFARE_Read() failed: "));
              Serial.println(mfrc522.GetStatusCodeName(status));
            }
            Serial.print(F("Data in block ")); Serial.print(blockAddr); Serial.println(F(":"));
            dump_byte_array(buffer, 16); Serial.println();
            Serial.println();
  
            String upc = "";
            String tempString;
            int tempint;
  
            for (byte i = 0; i < 7; i++) {
             
              
              byte temp = buffer[i];
  
              if((buffer[i] == 0x00) || (buffer[i] == 0x01) || (buffer[i] == 0x02) || (buffer[i] == 0x03) || (buffer[i] == 0x04) || (buffer[i] == 0x05) || (buffer[i] == 0x06) || (buffer[i] == 0x07) || (buffer[i] == 0x08) || (buffer[i] == 0x09) || (buffer[i] == 0x0A) || (buffer[i] == 0x0B) || (buffer[i] == 0x0C) || (buffer[i] == 0x0D) || (buffer[i] == 0x0E) || (buffer[i] == 0x0F)) {
                upc.concat("0"); 
              }
  
              if(i != 6) {
                tempint = (int) temp;  
                tempString = String(tempint);
              } else {
                tempint = (int) temp; 
                tempint = tempint / 10;
                tempString = String(tempint);
              }
  
              
      
  
              
              
              upc.concat(tempString);
              
              
            }
  
            String prodDate = "";
            for (byte i = 7; i < 11; i++) {
              
              
              byte temp = buffer[i];
  
              if((buffer[i] == 0x00) || (buffer[i] == 0x01) || (buffer[i] == 0x02) || (buffer[i] == 0x03) || (buffer[i] == 0x04) || (buffer[i] == 0x05) || (buffer[i] == 0x06) || (buffer[i] == 0x07) || (buffer[i] == 0x08) || (buffer[i] == 0x09) || (buffer[i] == 0x0A) || (buffer[i] == 0x0B) || (buffer[i] == 0x0C) || (buffer[i] == 0x0D) || (buffer[i] == 0x0E) || (buffer[i] == 0x0F)) {
                prodDate.concat("0"); 
              }
  
              if(i != 6) {
                tempint = (int) temp;  
                tempString = String(tempint);
              } else {
                tempint = (int) temp; 
                tempint = tempint / 10;
                tempString = String(tempint);
              }
  
              
      
  
              
              
              prodDate.concat(tempString);
              
              
            }
  
  
            String expiryDate = "";
            for (byte i = 11; i < 15; i++) {
              
              
              byte temp = buffer[i];
  
              if((buffer[i] == 0x00) || (buffer[i] == 0x01) || (buffer[i] == 0x02) || (buffer[i] == 0x03) || (buffer[i] == 0x04) || (buffer[i] == 0x05) || (buffer[i] == 0x06) || (buffer[i] == 0x07) || (buffer[i] == 0x08) || (buffer[i] == 0x09) || (buffer[i] == 0x0A) || (buffer[i] == 0x0B) || (buffer[i] == 0x0C) || (buffer[i] == 0x0D) || (buffer[i] == 0x0E) || (buffer[i] == 0x0F)) {
                expiryDate.concat("0"); 
              }
  
              if(i != 6) {
                tempint = (int) temp;  
                tempString = String(tempint);
              } else {
                tempint = (int) temp; 
                tempint = tempint / 10;
                tempString = String(tempint);
              }
  
              
      
  
              
              
              expiryDate.concat(tempString);
              
              
            }
  
            String dateOpen = "";
            for (byte i = 15; i < 16; i++) {
              
              
              byte temp = buffer[i];
  
              if((buffer[i] == 0x00) || (buffer[i] == 0x01) || (buffer[i] == 0x02) || (buffer[i] == 0x03) || (buffer[i] == 0x04) || (buffer[i] == 0x05) || (buffer[i] == 0x06) || (buffer[i] == 0x07) || (buffer[i] == 0x08) || (buffer[i] == 0x09) || (buffer[i] == 0x0A) || (buffer[i] == 0x0B) || (buffer[i] == 0x0C) || (buffer[i] == 0x0D) || (buffer[i] == 0x0E) || (buffer[i] == 0x0F)) {
                dateOpen.concat("0"); 
              }
  
              if(i != 6) {
                tempint = (int) temp;  
                tempString = String(tempint);
              } else {
                tempint = (int) temp; 
                tempint = tempint / 10;
                tempString = String(tempint);
              }
  
              
      
  
              
              
              dateOpen.concat(tempString);
              
              
            }
            Serial.println();
            Serial.println("UPC: "); Serial.print(upc);
            Serial.println("Production Date: "); Serial.print(prodDate);
            Serial.println("Expiry Date: "); Serial.print(expiryDate);
            Serial.println("Days after opening: "); Serial.print(dateOpen);

            // Authenticate using key B
            Serial.println(F("Authenticating again using key B..."));
            status = (MFRC522::StatusCode) mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_B, trailerBlock, &key, &(mfrc522.uid));
            if (status != MFRC522::STATUS_OK) {
                Serial.print(F("PCD_Authenticate() failed: "));
                Serial.println(mfrc522.GetStatusCodeName(status));
                return;
            }
            
            // Halt PICC
            mfrc522.PICC_HaltA();
            // Stop encryption on PCD
            mfrc522.PCD_StopCrypto1();

            for(byte i = 0xCF; i >= 0xC1; i = i-2) {
                digitalWrite(stb, LOW);
                shiftOut(dio, clk, LSBFIRST, i);
                shiftOut(dio, clk, LSBFIRST, 0x00);
                digitalWrite(stb, HIGH);
                delay(100);
             }

             playMelody();

            int freezeURL = 0;
            if(freeze) {
              freezeURL = 1;
            } 
  
            link = "http://anderledes.net/ddu19/createrecord.php?deviceid=123456&upc=" + upc + "&bestbefore=" + expiryDate + "&afteropening=" + dateOpen + "&qty=" + qty + "&freeze=" + freezeURL;
  
            http.begin(link);  //Specify request destination
            int httpCode = http.GET();                                                                  //Send the request
  
            payload = http.getString();   //Get the request response payload
            Serial.println(payload);

            
             delay(2000);
             

              }
            }
            
            
          } else if (keyMatrix == 4) {
             delay(1000);
            int qty = 1;
            bool freeze = false;
            byte KeyMatrix = readButtons();
            while((KeyMatrix == 0x02) || (KeyMatrix == 0x04) || (KeyMatrix == 0x08) || (KeyMatrix == 0x10) || (KeyMatrix == 0x20)) {
              delay(100);
              KeyMatrix = readButtons();
              
            }
            keyMatrix = readButtons();
            while(keyMatrix != 4) {


              if(qty > 10) {
                writeScreen(1, twoDigitToSegment3(qty));
                writeScreen(2, twoDigitToSegment4(qty));
                for(int i = 3; i < 9; i++) {
                  writeScreen(i, 0x00);
                }


                writeFreeze(freeze);
              } else {

                writeScreen(1, twoDigitToSegment4(qty));
                for(int i = 2; i < 9; i++) {
                  writeScreen(i, 0x00);
                }

                writeFreeze(freeze);
              }
              delay(500);

              keyMatrix = readButtons();
              switch (keyMatrix) {
                case 2:
                  break;
                case 8:
                  qty++;
                  break;
                case 16:
                  qty +=5;
                  break;
                case 32:
                  qty--;
                  break;
                case 64:
                  qty -= 5;
                  break; 
                case 128:
                  freeze = !freeze;
                  break;
  
              }
            }

            

              
              
            

            for(byte i = 0xC1; i <= 0xCF; i = i+2) {
              digitalWrite(stb, LOW);
              shiftOut(dio, clk, LSBFIRST, i);
              shiftOut(dio, clk, LSBFIRST, 0xFF);
              digitalWrite(stb, HIGH);
              delay(50);
             }
                        
            if (mfrc522.PICC_IsNewCardPresent()) {
              if (mfrc522.PICC_ReadCardSerial()) {
                // Show some details of the PICC (that is: the tag/card)
            Serial.print(F("Card UID:"));
            dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
            Serial.println();
            Serial.print(F("PICC type: "));
            MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
            Serial.println(mfrc522.PICC_GetTypeName(piccType));
  
            // Check for compatibility
            if (    piccType != MFRC522::PICC_TYPE_MIFARE_MINI
                    &&  piccType != MFRC522::PICC_TYPE_MIFARE_1K
                    &&  piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
              Serial.println(F("This sample only works with MIFARE Classic cards."));
              return;
            }
  
            byte sector         = 15;
            byte blockAddr      = 60;
            byte trailerBlock   = 63;
            MFRC522::StatusCode status;
            byte buffer[18];
            byte size = sizeof(buffer);
  
  
            // Authenticate using key A
            Serial.println(F("Authenticating using key A..."));
            status = (MFRC522::StatusCode) mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
            if (status != MFRC522::STATUS_OK) {
              Serial.print(F("PCD_Authenticate() failed: "));
              Serial.println(mfrc522.GetStatusCodeName(status));
              return;
            }
  
            // Show the whole sector as it currently is
            Serial.println(F("Current data in sector:"));
            mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);
            Serial.println();
  
  
            // Read data from the block
            Serial.print(F("Reading data from block ")); Serial.print(blockAddr);
            Serial.println(F(" ..."));
            status = (MFRC522::StatusCode) mfrc522.MIFARE_Read(blockAddr, buffer, &size);
            if (status != MFRC522::STATUS_OK) {
              Serial.print(F("MIFARE_Read() failed: "));
              Serial.println(mfrc522.GetStatusCodeName(status));
            }
            Serial.print(F("Data in block ")); Serial.print(blockAddr); Serial.println(F(":"));
            dump_byte_array(buffer, 16); Serial.println();
            Serial.println();
  
            String upc = "";
            String tempString;
            int tempint;
  
            for (byte i = 0; i < 7; i++) {
             
              
              byte temp = buffer[i];
  
              if((buffer[i] == 0x00) || (buffer[i] == 0x01) || (buffer[i] == 0x02) || (buffer[i] == 0x03) || (buffer[i] == 0x04) || (buffer[i] == 0x05) || (buffer[i] == 0x06) || (buffer[i] == 0x07) || (buffer[i] == 0x08) || (buffer[i] == 0x09) || (buffer[i] == 0x0A) || (buffer[i] == 0x0B) || (buffer[i] == 0x0C) || (buffer[i] == 0x0D) || (buffer[i] == 0x0E) || (buffer[i] == 0x0F)) {
                upc.concat("0"); 
              }
  
              if(i != 6) {
                tempint = (int) temp;  
                tempString = String(tempint);
              } else {
                tempint = (int) temp; 
                tempint = tempint / 10;
                tempString = String(tempint);
              }
  
              
      
  
              
              
              upc.concat(tempString);
              
              
            }
  
            String prodDate = "";
            for (byte i = 7; i < 11; i++) {
              
              
              byte temp = buffer[i];
  
              if((buffer[i] == 0x00) || (buffer[i] == 0x01) || (buffer[i] == 0x02) || (buffer[i] == 0x03) || (buffer[i] == 0x04) || (buffer[i] == 0x05) || (buffer[i] == 0x06) || (buffer[i] == 0x07) || (buffer[i] == 0x08) || (buffer[i] == 0x09) || (buffer[i] == 0x0A) || (buffer[i] == 0x0B) || (buffer[i] == 0x0C) || (buffer[i] == 0x0D) || (buffer[i] == 0x0E) || (buffer[i] == 0x0F)) {
                prodDate.concat("0"); 
              }
  
              if(i != 6) {
                tempint = (int) temp;  
                tempString = String(tempint);
              } else {
                tempint = (int) temp; 
                tempint = tempint / 10;
                tempString = String(tempint);
              }
  
              
      
  
              
              
              prodDate.concat(tempString);
              
              
            }
  
  
            String expiryDate = "";
            for (byte i = 11; i < 15; i++) {
              
              
              byte temp = buffer[i];
  
              if((buffer[i] == 0x00) || (buffer[i] == 0x01) || (buffer[i] == 0x02) || (buffer[i] == 0x03) || (buffer[i] == 0x04) || (buffer[i] == 0x05) || (buffer[i] == 0x06) || (buffer[i] == 0x07) || (buffer[i] == 0x08) || (buffer[i] == 0x09) || (buffer[i] == 0x0A) || (buffer[i] == 0x0B) || (buffer[i] == 0x0C) || (buffer[i] == 0x0D) || (buffer[i] == 0x0E) || (buffer[i] == 0x0F)) {
                expiryDate.concat("0"); 
              }
  
              if(i != 6) {
                tempint = (int) temp;  
                tempString = String(tempint);
              } else {
                tempint = (int) temp; 
                tempint = tempint / 10;
                tempString = String(tempint);
              }
  
              
      
  
              
              
              expiryDate.concat(tempString);
              
              
            }
  
            String dateOpen = "";
            for (byte i = 15; i < 16; i++) {
              
              
              byte temp = buffer[i];
  
              if((buffer[i] == 0x00) || (buffer[i] == 0x01) || (buffer[i] == 0x02) || (buffer[i] == 0x03) || (buffer[i] == 0x04) || (buffer[i] == 0x05) || (buffer[i] == 0x06) || (buffer[i] == 0x07) || (buffer[i] == 0x08) || (buffer[i] == 0x09) || (buffer[i] == 0x0A) || (buffer[i] == 0x0B) || (buffer[i] == 0x0C) || (buffer[i] == 0x0D) || (buffer[i] == 0x0E) || (buffer[i] == 0x0F)) {
                dateOpen.concat("0"); 
              }
  
              if(i != 6) {
                tempint = (int) temp;  
                tempString = String(tempint);
              } else {
                tempint = (int) temp; 
                tempint = tempint / 10;
                tempString = String(tempint);
              }
  
              
      
  
              
              
              dateOpen.concat(tempString);
              
              
            }
            Serial.println();
            Serial.println("UPC: "); Serial.print(upc);
            Serial.println("Production Date: "); Serial.print(prodDate);
            Serial.println("Expiry Date: "); Serial.print(expiryDate);
            Serial.println("Days after opening: "); Serial.print(dateOpen);

            // Authenticate using key B
            Serial.println(F("Authenticating again using key B..."));
            status = (MFRC522::StatusCode) mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_B, trailerBlock, &key, &(mfrc522.uid));
            if (status != MFRC522::STATUS_OK) {
                Serial.print(F("PCD_Authenticate() failed: "));
                Serial.println(mfrc522.GetStatusCodeName(status));
                return;
            }
            
            // Halt PICC
            mfrc522.PICC_HaltA();
            // Stop encryption on PCD
            mfrc522.PCD_StopCrypto1();

            for(byte i = 0xCF; i >= 0xC1; i = i-2) {
                digitalWrite(stb, LOW);
                shiftOut(dio, clk, LSBFIRST, i);
                shiftOut(dio, clk, LSBFIRST, 0x00);
                digitalWrite(stb, HIGH);
                delay(100);
             }

             playMelody();

            int freezeURL = 0;
            if(freeze) {
              freezeURL = 1;
            } 
  
            link = "http://anderledes.net/ddu19/removerecord.php?deviceid=123456&upc=" + upc + "&bestbefore=" + expiryDate + "&afteropening=" + dateOpen + "&qty=" + qty + "&freeze=" + freezeURL;
  
            http.begin(link);  //Specify request destination
            int httpCode = http.GET();                                                                  //Send the request
  
            payload = http.getString();   //Get the request response payload
            Serial.println(payload);

            
             delay(2000);
             

              }
            }
          }
          


        }

        http.end();   //Close connection
      } else if (s == WL_NO_SSID_AVAIL) {
        WiFi.disconnect();


      }
    }
    if (s == WL_CONNECTED) {
      MDNS.update();
    }
  }
  // Do work:
  //DNS
  dnsServer.processNextRequest();
  //HTTP
  server.handleClient();
}

