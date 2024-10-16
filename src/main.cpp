#include <Arduino.h>
#include <WiFi.h>
#include <Config.h>
#include <PubSubClient.h>

// libs for oled display 128x32
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// display properties
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

#define OLED_RESET -1       // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char *ssid = SSID;         // WiFI Name
const char *password = PASSWORD; // WiFi Password
const char *mqttServer = MQTTSERVER;
const char *mqttUser = MQTTUSER;
const char *mqttPassword = MQTTPASS;
const char *clientID = CLIENTID; // client id
const char *topicR = TOPICR;     // topic for receiving commands
const char *topicP = TOPICP;     // topic for sending data
const char *topicD = TOPICD;     // topic for debug

const int mqtt_timeout = 1; // constant for the mqtt timeout set to 1 second

#define echoPin 25  // attach pin 25 ESP32 to pin Echo of HC-SR04
#define trigPin 26  // attach pin 26 ESP32 to pin Trig of HC-SR04
#define relayPin 33 // attach pin 33 ESP32 to pin Relay

// define variables
bool pump_running = false;        // variable for the pump status
bool pump_switch = false;         // variable for the pump switch status
int pump_start_level = 0;         // variable for the pump start level
int dry_run_check_interval = 360; // variable for the dry run check interval value of 360 seconds = 6 minutes
int dry_run_check_counter = 0;    // variable for the dry run counter
bool dry_run_wait = false;        // variable for the dry run flag
int dry_run_wait_counter = 0;     // variable for the dry run wait counter
int dry_run_wait_interval = 5400; // variable for the dry run wait interval value of 5400 seconds = 90 minutes

// define constants
const int max_range = 450;           // constant for the maximum range of the sensor
const float speed_of_sound = 0.0343; // constant for the speed of sound in cm/s
const int water_stop_distance = 25;  // constant for the water stop distance
const int tank_height = 140;         // constant for the tank height
const int water_level_low = 100;     // constant for the water level low

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (80)
char msg[MSG_BUFFER_SIZE];
int value = 0;

void fillRectangle(int x, int y, int width, int height, int color)
{
    display.fillRect(x, y, width, height, color);
    display.display();
}

void displayWifiStatus(int status)
{
    int x = 0;
    int y = 16;
    int width = 64;
    int height = 8;
    fillRectangle(x, y, width, height, SSD1306_BLACK);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(x, y);
    if (status == 1)
    {
        display.println("Wifi On");
    }
    else if (status == 0)
    {
        display.println("Wifi Off");
    }
    else
    {
        display.println("Wifi Error");
    }
    display.display();
}

void displayMqttStatus(int status)
{
    delayMicroseconds(20);
    int x = 64;
    int y = 16;
    int width = 64;
    int height = 8;
    fillRectangle(x, y, width, 8, SSD1306_BLACK);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(x, y);
    if (status == 1)
    {
        display.println("Mqtt On");
    }
    else if (status == 0)
    {
        display.println("Mqtt Off");
    }
    else
    {
        display.println("Mqtt Error");
    }
    display.display();
}

void displayWaterLevel(int level)
{
    int x = 0;
    int y = 0;
    int width = 128;
    int height = 15;
    fillRectangle(x, y, width, height, SSD1306_BLACK);
    display.setCursor(1, 0);
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.println("Level " + String(level) + "%");
    display.display();
}

void displayPumpStatus(int status)
{
    delayMicroseconds(20);
    int x = 0;
    int y = 24;
    int width = 80;
    int height = 8;
    fillRectangle(x, y, width, height, SSD1306_BLACK);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(x, y);
    if (status == 1)
    {
        display.println("Pump Running");
    }
    else if (status == 0)
    {
        display.println("Pump Stopped");
    }
    else if (status == 2)
    {
        display.println("Pump Paused");
    }
    else
    {
        display.println("Pump Error");
    }
    display.display();
}

void setup_wifi()
{
    delay(10);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi ..");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print('.');
        delay(1000);
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    displayWifiStatus(1);
}

void reconnectWifi()
{
    Serial.println("WiFi disconnected. reconnecting...");
    delay(10);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
        displayWifiStatus(2);
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    displayWifiStatus(1);
}

void reconnectMqtt()
{
    while (!client.connected())
    {
        if (client.connect(clientID, mqttUser, mqttPassword))
        {
            Serial.println("MQTT connected");
            // client.setSocketTimeout(mqtt_timeout);
            displayMqttStatus(1);
            // ... and resubscribe
            client.subscribe(topicR);
        }
        else
        {
            displayMqttStatus(2);
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

String ipToString(IPAddress ip)
{
    String s = "";
    for (int i = 0; i < 4; i++)
        s += i ? "." + String(ip[i]) : String(ip[i]);
    return s;
}

void sendRSSIInfo()
{
    String ip = ipToString(WiFi.localIP());
    String temp = "{\"RSSI\":" + String(WiFi.RSSI()) + ",\"IP\":" + "\"" + ip + "\",\"Device\":\"ESP32\"}";
    Serial.println(temp);
    char tab2[1024];
    strncpy(tab2, temp.c_str(), sizeof(tab2));
    tab2[sizeof(tab2) - 1] = 0;
    client.publish(topicD, tab2);
}
long getDistance()
{
    digitalWrite(trigPin, LOW);                               // Sets the trigPin to LOW
    delayMicroseconds(2);                                     // waits 2 micro seconds
    digitalWrite(trigPin, HIGH);                              // Sets the trigPin to HIGH
    delayMicroseconds(20);                                    // waits 20 micro seconds
    digitalWrite(trigPin, LOW);                               // Sets the trigPin to LOW
    long duration = pulseIn(echoPin, HIGH, 26000);            // Reads the echoPin, returns the sound wave travel in microseconds
    int distance = duration * speed_of_sound;                 // Calculating the distance
    distance = distance / 2;                                  // Divide by 2 to remove the sound travel time of the echo to the distance
    Serial.println(" Distance: " + String(distance) + " cm"); // print the distance in Serial Monitor
    return distance;                                          // returns the distance in cm
}

int getWaterLevel(int distance)
{
    int water_level = tank_height - distance;
    return water_level;
}

int getWaterLevelInPercentage(int level)
{

    int per = (level + water_stop_distance) * 100 / tank_height;
    if (per <= 100)
    {
        return per;
    }
    return 100;
}

bool isTankFull(int level)
{
    if (level >= 100)
    {
        return true;
    }
    return false;
}

void stopPump()
{
    Serial.println(" Stopping Pump");
    delay(15000); // delay for 15 seconds
    // digitalWrite(pumpStopPin, HIGH); // turn on pump stop pin
    digitalWrite(relayPin, LOW);
    // digitalWrite(dryRunPin, LOW); // turn off dry run pin
    dry_run_wait = false;
    pump_running = false;
    // reset dry run counter
    dry_run_check_counter = 0;
    pump_switch = false;
    Serial.println(" Pump Stopped");
    displayPumpStatus(0);
}

void startPump(int level)
{
    if (isTankFull(level))
    {
        Serial.println(" Tank is full");
        stopPump();
        return;
    }
    Serial.println(" Starting Pump");
    delay(2000); // delay for 2 seconds
    digitalWrite(relayPin, HIGH);
    // digitalWrite(dryRunPin, LOW);   // turn off dry run pin
    // digitalWrite(pumpStopPin, LOW); // turn off pump stop pin
    pump_running = true;
    dry_run_check_counter = dry_run_check_interval;
    pump_start_level = level;
    pump_switch = true;
    Serial.println(" Pump Started");
    displayPumpStatus(1);
    displayWaterLevel(level);
}

void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    String message;
    for (int i = 0; i < length; i++)
    {
        message = message + (char)payload[i];
    }
    Serial.print(message);
    if (message == "TurnOn")
    {
        Serial.println("Start pump message received");
        client.publish(topicD, "{\"message\":\"Start pump command received\"}");
        int distance = getDistance();
        int level = getWaterLevel(distance);
        pump_start_level = getWaterLevelInPercentage(level);
        startPump(pump_start_level);
    }
    else if (message == "TurnOff")
    {
        Serial.println("Stop pump message received");
        client.publish(topicD, "{\"message\":\"Stop pump message received\"}");
        stopPump();
    }
    else
    {
        Serial.println("Unknown message received");
        client.publish(topicD, "{\"message\":\"Unknown message received\"}");
    }
}

void setUpWaterLevelController()
{
    int distance = getDistance();
    int level = getWaterLevel(distance);
    pump_start_level = getWaterLevelInPercentage(level);
    startPump(pump_start_level);                                    // turn on pump
    Serial.println(" Maximum Range: " + String(max_range) + " cm"); // print maximum range in Serial Monitor
}

void pausePump()
{
    Serial.println(" Pausing Pump");
    delay(2000); // delay for 2 seconds
    digitalWrite(relayPin, LOW);
    // digitalWrite(pumpStopPin, HIGH); // turn on pump stop pin
    // digitalWrite(dryRunPin, HIGH);   // turn on dry run pin
    pump_running = false;
    // reset dry run counter
    dry_run_check_counter = 0;
    Serial.println(" Pump Paused");
    displayPumpStatus(2);
}

bool isWaterFlowing(int level)
{
    if (level > pump_start_level)
    {
        return true;
    }
    return false;
}

void processDryRunProtect(int level)
{
    Serial.println(" Dry dry run protect engaged");
    if (dry_run_wait)
    {
        Serial.println(" Dry run wait: " + String(dry_run_wait_counter));

        if (dry_run_wait_counter > 0)
        {
            dry_run_wait_counter--;
        }
        else
        {
            dry_run_wait = false;
            dry_run_wait_counter = 0;
            dry_run_check_counter = dry_run_check_interval;
            startPump(level);
        }
        return;
    }

    Serial.println(" Dry run check: " + String(dry_run_check_counter));

    if (dry_run_check_counter > 0)
    {
        dry_run_check_counter--;
    }
    else
    {
        if (isWaterFlowing(level))
        {
            Serial.println(" Water is flowing ");
            dry_run_check_counter = dry_run_check_interval;
            dry_run_wait = false;
            pump_start_level = level;
        }
        else
        {
            Serial.println(" Water is not flowing ");
            // stop pump and enable dry run wait
            pausePump();
            dry_run_wait = true;
            dry_run_wait_counter = dry_run_wait_interval;
        }
    }
}

String pumpStatus()
{
    if (pump_running)
    {
        Serial.println(" Pump is running ");
        displayPumpStatus(1);
        return "Running";
    }
    else if (dry_run_wait)
    {
        Serial.println(" Pump is waiting for dry run ");
        displayPumpStatus(2);
        return "Paused";
    }
    else
    {
        Serial.println(" Pump is not running ");
        displayPumpStatus(0);
        return "Stopped";
    }
}

void waterLevelController()
{
    int distance = getDistance();
    int level = getWaterLevel(distance);
    int per = getWaterLevelInPercentage(level);
    // max distance is 300 cm
    if (distance > max_range)
    {
        Serial.println(" Tank Height is more than supported range of " + String(max_range) + " cm");
        // As a precaution stop pump
        stopPump();
    }
    else if (level < 0)
    {
        Serial.println(" Sensor distance is more than the tank capacity");
        // As a precaution stop pump
        stopPump();
    }
    else
    {
        // stop at water_stop_distance in cm
        if (distance <= water_stop_distance && pump_running)
        {
            stopPump();
        }
        // start if water level goes beyond water_level_low in cm
        else if (distance > water_level_low && !pump_running && !dry_run_wait)
        {
            pump_start_level = level;
            startPump(per);
        }

        // water level in percentage
        int per = getWaterLevelInPercentage(level);
        Serial.print(" Water level is " + String(per) + "%");
        displayWaterLevel(per);

        if (pump_switch)
        {
            processDryRunProtect(per);
        }

        String status = pumpStatus();
        String temp = "{\"Level\":" + String(per) + ",\"Distance\":" + String(distance) + ",\"PumpStatus\":" + "\"" + String(status) + "\"" + ",\"DryRunWait\":" + String(dry_run_wait) + ",\"MaxRange\":" + String(max_range) + ",\"WaterStopDistance\":" + String(water_stop_distance) + ",\"TankHeight\":" + String(tank_height) + ",\"LowWaterLevel\":" + String(water_level_low) + ",\"Unit\":\"CM\"}";
        Serial.println(temp);
        char tab2[1024];
        strncpy(tab2, temp.c_str(), sizeof(tab2));
        tab2[sizeof(tab2) - 1] = 0;
        client.publish(topicP, tab2);
    }
}

void setup()
{
    pinMode(trigPin, OUTPUT);  // Sets the trigPin as an OUTPUT
    pinMode(echoPin, INPUT);   // Sets the echoPin as an INPUT
    pinMode(relayPin, OUTPUT); // Sets the relayPin as an OUTPUT
    Serial.begin(115200);      // Serial Communication is starting with 9600 of baudrate speed

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
    {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;)
            ; // Don't proceed, loop forever
    }

    // Clear display buffer
    display.clearDisplay();
    display.display();

    Serial.println(" Ultrasonic Sensor HC-SR04");          // print some text in Serial Monitor
    Serial.println(" with ESP8266 and mqtt PubSubClient"); // print some text in Serial Monitor
    // intial delay of 5 seconds before starting pump
    Serial.println(" Initializing... ");
    int i = 5;
    while (i > 0)
    {
        Serial.println(i);
        delay(1000);
        i--;
    }
    Serial.println(" Initialization complete");

    // setUpWaterLevelController(); // call the function to set up the water level controller

    // During Starting WiFi LED should TURN OFF
    // digitalWrite(wifiLed, LOW);
    displayWifiStatus(0);

    // digitalWrite(mqttLed, LOW);
    displayMqttStatus(0);

    setup_wifi();
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());

    client.setServer(mqttServer, 1883);
    client.setCallback(callback);
    // setUpWaterLevelController();
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        // WiFi is not connected
        displayWifiStatus(0);
        // reconnect wifi
        reconnectWifi();
    }
    else
    {
        // WiFi is connected
        displayWifiStatus(1);

        // mqtt conditional reconnection
        if (!client.connected())
        {
            reconnectMqtt();
        }
        client.loop();
    }

    waterLevelController(); // call the function to control the water level
    sendRSSIInfo();
    // Get status every one seconds
    delay(1000); // delay for 1 second
}
