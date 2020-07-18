#include <Adafruit_FONA.h>
#include <SoftwareSerial.h>
#include <Adafruit_SSD1306.h>


// PINS
#define gpsTryLed 5
#define gpsSuccessLed 6
#define pStat  7
#define vio 8
#define batLatch 9
#define loopLed 10
#define fonaFiveVoltIn 11
#define key 12


// FONA setup
#define FONA_RX 2
#define FONA_TX 3
#define FONA_RST 4

SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;
Adafruit_FONA fona = Adafruit_FONA(FONA_RST);

// Display Setup
#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);
bool loopDot = false;

// Five Volt
int lastFiveVolt;

// Battery
//bool batIsLatched = false;
uint16_t vbat;
bool haveBat = false;

// GPS
//float latitude, longitude;
char LAT[15];
char LONG[15];
bool haveGps = false;
int8_t fixStat;

// SMS
int8_t cellStat;
char message[23];
// message 00.000000,00.000000,100

// DEBUG Flag
bool debug = false;


void setup() {
  while (!Serial);
  Serial.begin(9600);
  Serial.println("SETUP START");

  // Display
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x32)
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Carbot v2");
  display.setTextSize(1);
  display.print("Initializing...");
  display.display();
 

  // Outputs
  pinMode(batLatch, OUTPUT);
  pinMode(vio, OUTPUT);
  pinMode(key, OUTPUT);
  pinMode(gpsSuccessLed, OUTPUT);
  pinMode(gpsTryLed, OUTPUT);
  pinMode(loopLed, OUTPUT);

  // Inputs
  pinMode(pStat, INPUT);
  pinMode(fonaFiveVoltIn, INPUT);

  // Initial Pinout Settings
  bat_latch(true);                      // Ensures that the Arduino will stay on when 5V is cut
  digitalWrite(vio, HIGH);              // Required for FONA to talk at 3.3V logic level
  digitalWrite(gpsSuccessLed, LOW);     // A test pin which will go HIGH when GPS is locked.
  digitalWrite(gpsTryLed, LOW);         // A test pin which will go HIGH when GPS is attempted.
  digitalWrite(loopLed, LOW);

  int thisFiveVolt = digitalRead(fonaFiveVoltIn);
  lastFiveVolt = thisFiveVolt;
  int thisPStat = digitalRead(pStat);
  int thisBatLatch = digitalRead(batLatch);

  check_state(thisFiveVolt, thisPStat, thisBatLatch);

  Serial.println("SETUP END");

}


void loop() {

  digitalWrite(loopLed, HIGH);

  // Set five volt here so there's one reading
  int thisFiveVolt = digitalRead(fonaFiveVoltIn);
  int thisPStat = digitalRead(pStat);
  int thisBatLatch = digitalRead(batLatch);

  check_bat(1);
  check_fix();
  check_cell();
  get_gps(1);

  if (!thisFiveVolt && lastFiveVolt) {
    Serial.println("5v was turned off");
    check_bat(5);
    get_gps(15);
    if (haveBat && haveGps && !debug) send_sms();
    // Shutdown
    delay(1000);
    toggle_fona_pwr();
    delay(1000);
    bat_latch(false);
  }

  check_state(thisFiveVolt, thisPStat, thisBatLatch);

  lastFiveVolt = thisFiveVolt;

  digitalWrite(loopLed, LOW);
  draw_oled();
  //  delay(100);

}


void check_state(int thisFiveVolt, int thisPStat, int thisBatLatch) {

  Serial.println(F("BEGIN: Check state"));

  if (thisFiveVolt) {
    Serial.println(F("5v is HIGH"));
    if (!thisPStat) {
      Serial.println(F("FONA is OFF, turning it ON"));
      toggle_fona_pwr();
      delay(2000);
      start_fona_serial();
      delay(2000);
    }
    if (!thisBatLatch) {
      Serial.println(F("Bat is not latched, latching it"));
      bat_latch(true);
    }
    Serial.println("Checking FONA Serial");
    if (!fonaSS) {
      start_fona_serial();
    } else {
      Serial.println("FONA Serial is OK");
    }
  }


  if (!thisFiveVolt) {
    Serial.println(F("5v is LOW"));
    if (thisPStat) {
      Serial.println(F("FONA is ON, turning it OFF"));
      toggle_fona_pwr();
    }
    if (thisBatLatch) {
      Serial.println(F("Bat is latched, unlatching it"));
      bat_latch(false);
    }
  }

  Serial.println(F("END: Check state"));

}


void check_bat(int maxTries) {
  // Get Battery Percentage
  int batTries = 0;
  bool gotBat = false;

  do {

    gotBat = fona.getBattPercent(&vbat);

    if (gotBat) {
      haveBat = true;
    } else {
      // Set global haveBat
      haveBat = false;
    }
    batTries++;
    if (batTries < maxTries) delay(100);

  } while (batTries < maxTries && !gotBat);

}


void bat_latch(bool latch) {

  if (latch) {
    digitalWrite(batLatch, HIGH);
  }
  else {
    digitalWrite(batLatch, LOW);
  }

}


void toggle_fona_pwr() {

  digitalWrite(key, HIGH);
  delay(2000);
  digitalWrite(key, LOW);

}


void start_fona_serial() {
  Serial.println("Starting FONA Serial");
  fonaSerial->begin(4800);
  if (! fona.begin(*fonaSerial)) {
    while (1);
  }
  delay(1000);
  fona.enableGPS(true);
  delay(1000);

}


void send_sms() {

  // Create message
  sprintf(message, "%s,%s,%u", LAT, LONG, vbat);
  Serial.println("Sending message");
  Serial.println(message);
  bool messageSent = false;
  int smsTries = 0;

  Serial.println("In Send block");
  do {
    if (!fona.sendSMS("<<ENTER PHONE NUMBER HERE>>", message)) {
      Serial.println(F("Failed"));
      messageSent = false;
    } else {
      Serial.println(F("Sent!"));
      messageSent = true;
    }
    smsTries++;
  } while (smsTries <= 5 && !messageSent);

}


void get_gps(int maxTries) {

  bool gotGPS = false;
  int gpsTries = 0;
  float latitude, longitude;
  do {

    Serial.println(F("Getting GPS"));
    digitalWrite(gpsTryLed, HIGH);
    gotGPS = fona.getGPS(&latitude, &longitude);

    if (gotGPS) {
      dtostrf(latitude, 8, 6, LAT);
      dtostrf(longitude, 8, 6, LONG);
      haveGps = true;
      digitalWrite(gpsSuccessLed, haveGps);

    } else {
      haveGps = false;
      digitalWrite(gpsSuccessLed, haveGps);
      dtostrf(00.000000, 8, 6, LAT);
      dtostrf(00.000000, 8, 6, LONG);
    }
    digitalWrite(gpsTryLed, LOW);
    gpsTries++;
    if (gpsTries < maxTries) delay(250);

  } while (gpsTries < maxTries && !gotGPS);

}

void check_fix() {
  fixStat = fona.GPSstatus();
}

void check_cell() {
  cellStat = fona.getNetworkStatus();
}

void draw_oled(void) {

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("GPS: ");
  if (fixStat == 0) {
    display.print("Off");
  }
  else if (fixStat == 1) {
    display.print("None");
  }
  else if (fixStat == 2) {
    display.print("2D");
  }
  else if (fixStat == 3) {
    display.print("3D");
  }
  else {
    display.print("Err");
  }
  display.setCursor(64, 0);
  display.print("Cell: ");
  if (cellStat == 1) {
    display.println("Ok");
  }
  else {
    display.println("None");
  }
  display.print("vBat: ");
  display.print(vbat);
  display.println("%");
  display.print("Lat: ");
  display.println(LAT);
  display.print("Long: ");
  display.println(LONG);
  // draw dot
  if (loopDot) {
    display.setCursor(120, 25);
    display.write(7);
  } else {
    display.setCursor(120, 20);
    display.write(9);
  }
  loopDot = !loopDot;

  display.display();

}
