// You should use at least Arduino IDE 1.0.4, as the malloc/realloc bug is fixed on this release (needed for xPL to work)

#include <EEPROM.h>
#include <Timer.h> // https://github.com/ToniA/Timer/tree/master_v1.3 for the configurable amount of timers
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DS18B20.h>
#include <LiquidCrystal.h>
#include <avr/wdt.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <xPL.h> // Use https://github.com/ToniA/arduino-xpl as the xPL library

// xPL stuff, see the example xPL_Send_Arduino

xPL xpl;


// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
// This MAC/IP address pair is also set as a static lease on the router

byte macAddress[] = { 0x52, 0x8E, 0xD2, 0x5E, 0x20, 0xEC };
IPAddress ipAddress(192, 168, 0, 11);
IPAddress broadcastAddress(192, 168, 0, 255);
EthernetUDP Udp;

// Air-to-air heatpump control

const int heatpumpUdpPort = 49722; // Heatpump controller UDP port


// I/O assignments

const int LCD1 = 8;
const int LCD2 = 7;
const int LCD3 = 6;
const int LCD4 = 5;
const int LCD5 = 4;
const int LCD6 = 2;

const int ONEWIRE       = 9;
// Ethernet uses pins 10, 11, 12, 13 - yes, even the onboard LED
// Ethernet reset pin (faulty hardware design :)
const int ETHERNET_RST  = A0;

// Relays on the relay board
const int DEFROST       = A2;
const int SEWER_HEATING = A3;
const int BACKUP_HEATER = A4;
const int AWHP_RUN      = A5;

// Structs with device name and device address

typedef struct onewireSensor onewireSensor;
struct onewireSensor
{
    char *name;
    byte onewireAddress[8];
};

typedef struct DS18B20Sensor DS18B20Sensor;
struct DS18B20Sensor
{
    onewireSensor device;
    float temperature;
};


//
// DS18B20 sensors on OneWire
//

OneWire ow1(ONEWIRE);
DS18B20Sensor DS18B20Sensors[] = {
  {{ "Vaihdin l\xE1ht\xEF", { 0x28, 0x53, 0xC7, 0x84, 0x03, 0x00, 0x00, 0x29 } }, DEVICE_DISCONNECTED }, // 0 - 2m wire 'Heat exhanger - outlet' in Finnish
  {{ "Vaihdin tulo",        { 0x28, 0xBB, 0x00, 0x85, 0x03, 0x00, 0x00, 0x5D } }, DEVICE_DISCONNECTED }, // 1 - 2m wire 'Heat exhanger - inlet'
  {{ "Ulkona",              { 0x28, 0xD9, 0xD1, 0xB0, 0x03, 0x00, 0x00, 0xAA } }, DEVICE_DISCONNECTED }, // 2 - 3m wire - red stripe 'Outdoor'
  {{ "H\xEFyrystin",        { 0x28, 0xE0, 0xF5, 0x27, 0x05, 0x00, 0x00, 0x80 } }, DEVICE_DISCONNECTED }  // 3 - 3m wire 'Evaporator'
};

const int sensorOutlet   = 0;
const int sensorInlet    = 1;
const int sensorOutdoor  = 2;
const int sensorPipetemp = 3;


// Stock defrost threshold

byte defrostThreshold = 7;


// LCD pins

LiquidCrystal lcd( LCD1, LCD2, LCD3, LCD4, LCD5, LCD6 );


// Heatpump commands

static const prog_char downstairsHeatpumpOn[] PROGMEM      = "{\"command\":\"command\",\"identity\":\"02:26:89:B3:13:C5\",\"model\":\"panasonic_ckp\",\"power\":1,\"mode\":2,\"temperature\":22,\"fan\":0}";
static const prog_char downstairsHeatpumpOff[] PROGMEM     = "{\"command\":\"command\",\"identity\":\"02:26:89:B3:13:C5\",\"model\":\"panasonic_ckp\",\"power\":0,\"mode\":2,\"temperature\":22,\"fan\":0}";
static const prog_char upstairsHeatpumpOn[] PROGMEM        = "{\"command\":\"command\",\"identity\":\"02:26:89:28:25:C5\",\"model\":\"panasonic_dke\",\"power\":1,\"mode\":5,\"temperature\":22,\"fan\":2}"; // The upstairs heatpump is just running 'FAN' to better circulate the air
static const prog_char upstairsHeatpumpOff[] PROGMEM       = "{\"command\":\"command\",\"identity\":\"02:26:89:28:25:C5\",\"model\":\"panasonic_dke\",\"power\":0,\"mode\":2,\"temperature\":21,\"fan\":0}";


// Global variables

DallasTemperature owSensors(&ow1);
Timer timer(15);

int displayedSensor = 0;

boolean defrostCheating = false;
boolean awhpRunning     = true;
boolean heatpumpRunning = false;
boolean sewerHeating    = false;


//
// Set up
//
// - set serial mode
// - initialize Ethernet
// - initialize the defrost pin
// - initialize xPL
// - enable watchdog
// - initialize the LCD display
// - read the defrost threshold from EEPROM
// - list all DS18B20 sensors from the bus
// - take the first readings
// - initialize the timer routines

void setup(void) {

  int i;
  int j;
  byte onewireAddress[8];

  // Serial initialization

  Serial.begin(9600);
  Serial.println(F("Starting..."));

  // Ethernet shield reset trick
  // Need to cut the RESET lines (also from ICSP header) and connect an I/O (A1 in this case) to RESET on the shield

  pinMode(ETHERNET_RST, OUTPUT);
  digitalWrite(ETHERNET_RST, HIGH);
  delay(50);
  digitalWrite(ETHERNET_RST, LOW);
  delay(50);
  digitalWrite(ETHERNET_RST, HIGH);
  delay(100);

  // 4xRelay board initialization

  // Defrost cheating - off by default
  pinMode(DEFROST, OUTPUT);
  digitalWrite(DEFROST, HIGH);

  // Sewer heating cable - off by default
  pinMode(SEWER_HEATING, OUTPUT);
  digitalWrite(SEWER_HEATING, HIGH);

  // Backup heater cheating signal to Ouman - off by default
  pinMode(BACKUP_HEATER, OUTPUT);
  digitalWrite(BACKUP_HEATER, HIGH);

  // 'Don't Run' signal to Fujitsu heatpump - off by default
  pinMode(AWHP_RUN, OUTPUT);
  digitalWrite(AWHP_RUN, HIGH);


  // xPL initialization

  Ethernet.begin(macAddress,ipAddress);
  Udp.begin(xpl.udp_port);

  Serial.print(F("My IP address: "));
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    // print the value of each byte of the IP address:
    Serial.print(Ethernet.localIP()[thisByte], DEC);
    Serial.print(F("."));
  }
  Serial.println();

  xpl.SendExternal = &sendUdPMessage;  // pointer to the send callback
  xpl.ip = ipAddress;
  xpl.SetSource_P(PSTR("xpl"), PSTR("arduino"), PSTR("awhpcontroller")); // parameters for hearbeat message

  // Watchdog requires ADABOOT to work
  // See:
  // * http://www.ladyada.net/library/arduino/bootloader.html
  // * http://www.grozeaion.com/electronics/arduino/131-atmega328p-burning-bootloader.html

  wdt_enable(WDTO_8S);

  // Startup message on LCD

  lcd.begin(16, 2);
  lcd.print("K\xE1ynnistyy..."); // 'Starting...' in Finnish

  Serial.print(F("Defrost threshold is: "));
  Serial.println(defrostThreshold);

  // List OneWire devices

  owSensors.begin();
  owSensors.setWaitForConversion(false);
  j = owSensors.getDeviceCount();

  Serial.print(F("1-wire has "));
  Serial.print(j);
  Serial.println(F(" devices:"));

  for( displayedSensor = 0; displayedSensor < j; displayedSensor++) {
    owSensors.getAddress(onewireAddress, displayedSensor);

    Serial.print(F("ADDR="));
    for( i = 0; i < sizeof(onewireAddress); i++) {
      if (onewireAddress[i] < 0x10) {
        Serial.print(F("0"));
      }
      Serial.print(onewireAddress[i], HEX);
      if ( i == 0 ) {
        Serial.print(F("."));
      }
    }
    Serial.println(F(""));
  }

  displayedSensor = 0;

  // Take the initial readings from all OneWire devices

  owSensors.requestTemperatures();
  delay(1000);
  takeReading();

  // Timers - everything is based on timer events

  timer.every(10000, startReading);
  timer.after(20000, awhpDefrostSignal);       // Defrost reschedules itself using different intervals
  timer.after(30000, sewerHeatingCableSignal); // Sewer heating cable reschedules itself using different intervals
  timer.every(60000, awhpState);
  timer.every(60000, heatpumpState);
  timer.every(2000, feedWatchdog);
  timer.every(60000, sendxPL);
  timer.every(2000, updateDisplay);
}


//
// The main loop
// - update the timer to call the timed events
// - process incoming xPL messages
//

void loop(void) {
  timer.update();
  xpl.Process();  // xPL heartbeat management
}


//
// Start reading the DS18B20 sensors, schedule a one-time event to read the
// temperatures 1000 milliseconds later
//

void startReading()
{
  owSensors.requestTemperatures();
  timer.after(1000, takeReading);
}


//
// Read the DS18B20 sensors, as scheduled from startReading()
//

void takeReading()
{
  byte i;

  for ( i = 0; i < sizeof(DS18B20Sensors) / sizeof(DS18B20Sensor); i++ ) {
    DS18B20Sensors[i].temperature = owSensors.getTempC(DS18B20Sensors[i].device.onewireAddress);
  }
}


//
// Update the LCD screen with temperatures and state info
//

void updateDisplay()
{
  int numberOfSensors = sizeof(DS18B20Sensors) / sizeof(DS18B20Sensor);

  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);

  if ( displayedSensor < numberOfSensors ) {
    float celsius = DS18B20Sensors[displayedSensor].temperature;
    Serial.print(F("  Temperature of sensor "));
    Serial.print(displayedSensor);
    Serial.print(F("  = "));
    Serial.print(celsius);
    Serial.print(F(" C "));
    Serial.println(DS18B20Sensors[displayedSensor].device.name);

    lcd.print(DS18B20Sensors[displayedSensor].device.name);

    lcd.setCursor(0, 1);
    lcd.print(celsius);
    lcd.print(" \xDF""C");
  } else if (displayedSensor == numberOfSensors) {
    lcd.print("ON/OFF k\xE1ynti");
    lcd.setCursor(0, 1);
    if ( awhpRunning ) {
      lcd.print("* ON");
    } else {
      lcd.print("* OFF");
    }
  } else if (displayedSensor == numberOfSensors + 1) {
    lcd.print("Sulatushuijaus");
    lcd.setCursor(0, 1);
    if ( defrostCheating ) {
      lcd.print("* ON");
    } else {
      lcd.print("* OFF");
    }
  } else if (displayedSensor == numberOfSensors + 2) {
    lcd.print("L\xE1mmityskaapeli");
    lcd.setCursor(0, 1);
    if ( sewerHeating ) {
      lcd.print("* ON");
    } else {
      lcd.print("* OFF");
    }
  } else if (displayedSensor == numberOfSensors + 3) {
    lcd.print("ILP apul\xE1mmitys");
    lcd.setCursor(0, 1);
    if ( heatpumpRunning ) {
      lcd.print("* ON");
    } else {
      lcd.print("* OFF");
    }
  }

  displayedSensor++;
  if ( displayedSensor >= numberOfSensors + 4 )
  {
    displayedSensor = 0;
  }
}


//
// Update the AWHP defrost signal
//

void awhpDefrostSignal()
{
  unsigned long nextDefrostCheck = 60000; // 1 min

  Serial.print(F("Defrost cheating: outdoor "));
  Serial.println(DS18B20Sensors[sensorOutdoor].temperature);
  Serial.print(F("Defrost cheating: pipe "));
  Serial.println(DS18B20Sensors[sensorPipetemp].temperature);

  if (DS18B20Sensors[sensorOutdoor].temperature < -5.0 ) {
    if ( (DS18B20Sensors[sensorOutdoor].temperature - DS18B20Sensors[sensorPipetemp].temperature) < defrostThreshold &&
          DS18B20Sensors[sensorPipetemp].temperature < 0.0 ) {
      // outdoor below -5 and pipetemp less than defrost threshold below outdoor and defrosting is not in progress -> keep cheating on and check every minute
      digitalWrite(DEFROST, LOW);
      defrostCheating = true;
      Serial.println(F("Cheating"));
    } else {
      // outdoor below -5 and pipetemp below defrost threshold or defrosting is in progress -> defrost cheating off for next 15 minutes -> defrosts
      digitalWrite(DEFROST, HIGH);
      defrostCheating = false;
      Serial.println(F("Defrosting"));

      nextDefrostCheck = 900000L; // 15 min, give time to defrost
    }
  }
  else {
    digitalWrite(DEFROST, HIGH);
    defrostCheating = false;
    Serial.println(F("Not cheating"));

    nextDefrostCheck = 900000L; // 15 min
  }

  timer.after(nextDefrostCheck, awhpDefrostSignal);
}


//
// Update the sewer heating cable signal
//

void sewerHeatingCableSignal()
{
  unsigned long nextSewerCableCheck = 60000; // 1 min

  if (DS18B20Sensors[sensorOutdoor].temperature < -5.0 ) {
    // It's cold, run the heating all the time
    digitalWrite(SEWER_HEATING, LOW);
    sewerHeating = true;
    nextSewerCableCheck = 900000L; // 15 min
  } else if (DS18B20Sensors[sensorOutdoor].temperature < 0.0 && DS18B20Sensors[sensorPipetemp].temperature > 0.0) {
    // Probably defrosting
    digitalWrite(SEWER_HEATING, LOW);
    sewerHeating = true;
    nextSewerCableCheck = 900000L; // 15 min
  } else {
    // It's not below -5 and the heatpump is not defrosting
    digitalWrite(SEWER_HEATING, HIGH);
    sewerHeating = false;
  }

  timer.after(nextSewerCableCheck, sewerHeatingCableSignal);
}


//
// Update the AWHP heating/idle state
//
// This is an inverter device, but experience has shown that the COP is really poor when running on low power,
// so it's much better to sit idle for a while and then run on higher power
//

void awhpState()
{
  Serial.print(F("AWHP state: outdoor "));
  Serial.println(DS18B20Sensors[sensorOutdoor].temperature);
  Serial.print(F("AWHP state: boiler "));
  Serial.println(DS18B20Sensors[sensorInlet].temperature);

  if (awhpRunning) {
    // If the output power is low enough, turn off the heatpump and wait until the boiler cools down a bit
    // Backup heater is also turned off in this case
    if ( DS18B20Sensors[sensorOutlet].temperature > 35 &&
        (DS18B20Sensors[sensorOutlet].temperature - DS18B20Sensors[sensorInlet].temperature) < 3 &&
         DS18B20Sensors[sensorOutdoor].temperature > -5 ) {
      Serial.println(F("AWHP - stop running"));

      digitalWrite(AWHP_RUN, LOW);
      digitalWrite(BACKUP_HEATER, LOW);

      awhpRunning = false;
    } else {
      Serial.println(F("AWHP - keep on running"));

      digitalWrite(AWHP_RUN, HIGH);
      digitalWrite(BACKUP_HEATER, HIGH);

      awhpRunning = true;
    }
  }
  
  // Wait until the boiler has cooled down before starting again
  else {
    if ( DS18B20Sensors[sensorInlet].temperature < 30 ) {
      awhpRunning = true;
    }
  }

  Serial.println(F("AWHP state set"));
}


//
// Update the air-to-air heatpump states
// * Start the units when it's really cold (AWHP is not enough by itself)
// * Stop the units when it's not that cold any more
//
// These are separate units controlled by https://github.com/ToniA/arduino-wp-heatpump-controller
//

void heatpumpState()
{
  if ( defrostCheating == true ) { // Defrosting affects the outdoor temperature readings

    Serial.print(F("Heatpump state: outdoor "));
    Serial.println(DS18B20Sensors[sensorOutdoor].temperature);

    if ( DS18B20Sensors[sensorOutdoor].temperature < -11 && heatpumpRunning == false) {
      sendUdPProgmemMessage(downstairsHeatpumpOn, heatpumpUdpPort);
      sendUdPProgmemMessage(upstairsHeatpumpOn, heatpumpUdpPort);

      heatpumpRunning = true;
    } else if ( DS18B20Sensors[sensorOutdoor].temperature > -8 && heatpumpRunning == true) {
      sendUdPProgmemMessage(downstairsHeatpumpOff, heatpumpUdpPort);
      sendUdPProgmemMessage(upstairsHeatpumpOff, heatpumpUdpPort);

      heatpumpRunning = false;
    }

    Serial.println(F("Heatpump state set"));
  }
}


//
// UDP message sender for xPL
//

void sendUdPMessage(char *buffer)
{
    Udp.beginPacket(broadcastAddress, xpl.udp_port);
    Udp.write(buffer);
    Udp.endPacket();
}


//
// UDP message sender for heatpump control (from a prog_char type of buffer)
//

void sendUdPProgmemMessage(const prog_char *buffer, int udpPort)
{
  Udp.beginPacket(broadcastAddress, udpPort);

  prog_char* msg = (prog_char*)buffer;

  while (char msgChar = pgm_read_byte(msg++))
  {
    Udp.write(msgChar);
  }

  Udp.endPacket();
}

//
// Send the 1-wire sensor temperatures as xPL xpl-trig messages
// This way the home automation can log the temperatures
//

void sendxPL()
{
  byte i, j;

  char onewireAddress[32];
  char celsius[32];

  Serial.println(F("Sending xPL messages"));

  for ( i = 0; i < sizeof(DS18B20Sensors) / sizeof(DS18B20Sensor); i++ ) {
    // sprintf does not support %f -> custom 'ftoa' does it :)
    ftoa(celsius, DS18B20Sensors[i].temperature,1);

    // 1-wire address like 28.BACFB0030000EE
    sprintf_P(onewireAddress,
            PSTR("%02X.%02X%02X%02X%02X%02X%02X%02X"),
            DS18B20Sensors[i].device.onewireAddress[0],
            DS18B20Sensors[i].device.onewireAddress[1],
            DS18B20Sensors[i].device.onewireAddress[2],
            DS18B20Sensors[i].device.onewireAddress[3],
            DS18B20Sensors[i].device.onewireAddress[4],
            DS18B20Sensors[i].device.onewireAddress[5],
            DS18B20Sensors[i].device.onewireAddress[6],
            DS18B20Sensors[i].device.onewireAddress[7]);

    // send an xPL message

    Serial.print(F("Free RAM: "));
    Serial.println(freeRam());
    Serial.print(onewireAddress);
    Serial.print(F(" -> "));
    Serial.flush();
    Serial.print(celsius);
    Serial.println(F(" C"));

    xPL_Message xPLMessage;

    xPLMessage.hop = 1;
    xPLMessage.type = XPL_TRIG;

    xPLMessage.SetTarget_P(PSTR("*"));
    xPLMessage.SetSchema_P(PSTR("sensor"), PSTR("basic"));

    xPLMessage.AddCommand("device", onewireAddress);
    xPLMessage.AddCommand_P(PSTR("type"), PSTR("temp"));
    xPLMessage.AddCommand("current", celsius);
    xpl.SendMessage(&xPLMessage);
  }

  // Send info about the defrost cheat state

  xPL_Message xPLMessage;

  xPLMessage.hop = 1;
  xPLMessage.type = XPL_TRIG;

  xPLMessage.SetTarget_P(PSTR("*"));
  xPLMessage.SetSchema_P(PSTR("sensor"), PSTR("basic"));

  xPLMessage.AddCommand("device", "defrost");
  xPLMessage.AddCommand_P(PSTR("type"), PSTR("temp"));
  if (defrostCheating) {
    xPLMessage.AddCommand("current", "10");
  } else {
    xPLMessage.AddCommand("current", "20");
  }
  xpl.SendMessage(&xPLMessage);

  Serial.println(F("xPL messages sent"));
}


//
// Convert float to string
// See http://forum.arduino.cc/index.php/topic,44262.0.html
//

char *ftoa(char *a, double f, int precision)
{
  long p[] = {0,10,100,1000,10000,100000,1000000,10000000,100000000};

  char *ret = a;
  long heiltal = (long)f;
  itoa(heiltal, a, 10);
  while (*a != '\0') a++;
  *a++ = '.';
  long desimal = abs((long)((f - heiltal) * p[precision]));
  itoa(desimal, a, 10);
  return ret;
}


//
// Free RAM - for figuring out the reason to upgrade from IDE 1.0.1 to 1.0.5
// Returns the free RAM in bytes - you'd be surprised to see how little that is :)
// http://playground.arduino.cc/Code/AvailableMemory
//

int freeRam () {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

//
// The most important thing of all, feed the watchdog
//

void feedWatchdog()
{
  wdt_reset();
}
