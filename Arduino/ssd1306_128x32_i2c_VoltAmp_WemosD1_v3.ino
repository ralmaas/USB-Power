/**************************************************************************
 PSU-01

 Code for handling the interactions inside the PSU-01 box
 Interfacing to a ADC, an OLED display and the Hall Sensor

 Copyrights are located inside each of the .h files
 Major contributor is of cause AdaFruit !
2021-01-02/ralm V2 Production version
	Modified MQTT callback function
2021-01-03/ralm V3 Production version
	Added display of code version

 =========================================================
 Used the code as a template for my Voltage Source Project
  Mena Well 5V/10A power supply
  Hall-based Amp-meter
  ADC-converter ADS1115
  OLED (128x32) for display
  Button to activate and change mode on the display
    Amp & Voltage
    Graph current
      500mA, 1A, 2A, 5A and 10A
    Display will turn off after a couple of minutes
    A short press will reactivate display
    A long press will change display mode
  Assembly Box (140x110x35) From Kjell&Co

 Note: Code may still containg some debug stuff - primarily Print-statements
 **************************************************************************/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ADS1015.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "splash.h"
#define VERSION "Ver 3.0"
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET    -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define ARRAY 100     // x-points - adjust to graph/text
#define LOGO_WIDTH 128
#define LOGO_HEIGHT 32
#define BUTTON_PIN D3
#define QUARTERS 10000 // 100 // Seconds before display is turned off

#define ADC_RESOLUTION  0.000015625
#define ADC_GAIN        GAIN_EIGHT
#define ADC_VOLT_REF    0.1003   // Using 1k & 10k voltage divider
#define ADC_AMP_SCALE   0.0000606      // Datasheet saying 40mV/A

float values[ARRAY];  // values[0] is last read current
float diff000[10];  // values[0] is last read current

float V = 0.0;        // Last read Voltage
float rad;
unsigned long t1 = QUARTERS;  // number of 0.25 seconds to keep display active...
int mode = 0;
int range = 500;
bool disp_mode = 0;
int ADC_AMP_OFFSET = 144;          // Depending upon match of two equal resistors (10k)


// Update these with values suitable for your network.
const char* ssid = "Your SSID";
const char* password = "Your WiFi password";
const char* mqtt_server = "IP of MQTT-server";

Adafruit_SSD1306 display(OLED_RESET);
Adafruit_ADS1115 ads;
WiFiClient espClient;
PubSubClient client(espClient);


//====================================================
//  Support Code
//====================================================
void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}


void callback(char* topic, byte* payload, unsigned int length) {
  char buffer_payload[30];

  strncpy(buffer_payload, (char *) payload, length);
  buffer_payload[length] = '\0';
  // Switch Mode
  mode = atoi(buffer_payload);
  if((mode < 0) || (mode >5))
    mode = 0;

  t1 = QUARTERS;    // Enable display !!!!
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("PSU01/status", "Online");
      // ... and resubscribe
      client.subscribe("PSU01/cmd");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void drawMyBitmap(void) {
  display.clearDisplay();

  display.drawBitmap(
    (display.width()  - LOGO_WIDTH ) / 2,
    (display.height() - LOGO_HEIGHT) / 2,
    logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, 1);
  display.display();
  delay(500);
}

void displayBig(bool m) {
  display.clearDisplay();

  display.setTextSize(4);      // Normal 1:1 pixel scale
  display.setTextColor(WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
  if (m)
  {
    display.print(V, 2);
    display.print(F("V"));
  }
  else
  {
    display.print(values[0], 0);
    display.print(F("mA"));
  }
  display.display();
}

void plot_values(float range)
{
  int i, res;

  display.clearDisplay();
  for (i = 0; i < (ARRAY-30); i++)
  {
    res = int((values[i]/range) * 32);
    if (res > 31) 
    {
      for (int j=0; j < 31; j++)
      display.drawPixel(i, j, WHITE);
    }
    if (res >= 0)
      display.drawPixel(i, 31-res, WHITE);
  }
  display.setTextColor(WHITE); // Draw white text
  // Show range value in upper left cornet; smallest font
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print(range,0);

  // Show Volt and mA on right hand side in medium size
  display.setCursor(80, 0);
  display.setTextSize(2);
  display.print(V, 1); display.print("V");
  display.setCursor(80, 16);
  display.print(values[0]/1000.0, 2); // No space to show "mA" or "A"
  display.display();
}

void displayNewMode(int m)
{
  display.clearDisplay();
  display.setCursor(10, 10);
  display.setTextSize(3);      // Normal 1:1 pixel scale
  display.setTextColor(WHITE); // Draw white text
  display.print("Mode:" ); display.print(m);  
  display.display();
  delay(500);
}
void displayText(int x, int y, int text_size, String text)
{
  display.clearDisplay();
  display.setCursor(x, y);
  display.setTextSize(text_size);      // Normal 1:1 pixel scale
  display.setTextColor(WHITE); // Draw white text
  display.print(text);  
  display.display();
}

void calibrateAmp()
{
  // Perform 50 readout of amp
  int res = 0;
  int loop = 0;
  displayText(0, 10, 2, "Calibrate");
  for (loop=0; loop < 100; loop++)
  {
    res += ads.readADC_Differential_0_1();
    delay(10);
  }
  ADC_AMP_OFFSET = 0 - res / 100;
  char buffer[10];
  sprintf(buffer, "Offs: %d", ADC_AMP_OFFSET);
  displayText(0, 10, 2, buffer);
}

//====================================================
//  Setup
//====================================================
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Mode selection
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setRotation(2);   // Rotate set to 2 for pins at right; 0 for pins at left side
  
  ads.setGain(ADC_GAIN);        // 4x gain   +/- 1.024V  1 bit = 0.03125mV
  ads.begin();

  // Clear readout value array
  for (int i = 0; i < ARRAY; i++)
    values[i] = -1;
    
  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(2000); // Pause for 2 seconds

  // Clear the buffer
  display.clearDisplay();
  drawMyBitmap();
  displayText(0, 10, 2, VERSION);
  delay(100);
  calibrateAmp();
  delay(500);
  // Ready for WiFi/MQTT
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

}

//====================================================
//  Loop
//====================================================
void loop() {
  int16_t diff0, diff1;
  char  buffer[10];

  if (!client.connected()) {
    reconnect();
  }
  client.loop();
    
  // shift array
  for (int i=ARRAY-1; i > 0; i--)
    values[i] = values[i-1];

  // shift array
  for (int i=0; i < 9; i++)
    diff000[i] = diff000[i+1];

  // Get voltage
  diff1 = ads.readADC_Differential_2_3();
  V = diff1 * ADC_RESOLUTION/ADC_VOLT_REF;    // May need some tuning

  // Publish every x seconds
  if (t1%10 == 0)
  {
    sprintf(buffer, "%5.2f", V);
    client.publish("PSU01/voltage", buffer);
  }

  // Get Amp
  diff0 = 0-ads.readADC_Differential_0_1();
  diff000[9] = diff0;

  float avg = float(diff0 - ADC_AMP_OFFSET)/1.457;
  if (t1%10 == 0)
  {
    sprintf(buffer, "%5.0f", avg);
    client.publish("PSU01/amp", buffer);
  }

  int total = 0;
  for (int i=0; i < 10; i++)
    total += diff000[i];
    
  if (avg < 0)
    avg = 0;

  // Update array
  values[0] = avg;

  if (t1 > 0)   // Should display be activated ?
  {
    if (mode > 0)
    {
      if (mode == 1)
        range = 500;
      else
        if (mode == 2)
          range = 1000;
        else
          if (mode == 3)
            range = 2000;
          else
            if (mode == 4)
              range = 5000;
            else
              if (mode == 5)
                range = 10000;
      plot_values(range);
    } else // mode == 0
    {
      if (t1%3 == 0)
        disp_mode = !disp_mode;
      displayBig(disp_mode);
    }
  }

  // Handle button
  if (digitalRead(BUTTON_PIN) == 0)
  {
    // Delay
    unsigned long t0;
    t0 = millis();
    while ((millis() - t0) < 200);  // Wait 200 ms
    if ( digitalRead(BUTTON_PIN) == 0)
    {
      // OK - still low..
      mode++;
      if (mode >= 6)
        mode = 0;
      t1 = QUARTERS;    // Set count-down element
      displayNewMode(mode);
    }
    else
      t1 = QUARTERS;    // Very short press just reactivates display - no mode change
  }
  if (t1 > 0)
    t1--;
  if (t1 == 0)
  {
    // Turn off display
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(random(0, 40), random(0,20));
    display.print("Press key!");
    display.display();
  }
  delay(2000);    // Initial setting of 1 second

}
