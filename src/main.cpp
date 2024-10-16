#include <Arduino.h>
#include <WiFi.h>
#include <Config.h>
#include <PubSubClient.h>
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

// WiFi and MQTT credentials
const char *ssid = SSID;
const char *password = PASSWORD;
const char *mqttServer = MQTTSERVER;
const char *mqttUser = MQTTUSER;
const char *mqttPassword = MQTTPASS;
const char *clientID = CLIENTID;
const char *topicR = TOPICR; // topic for receiving command
const char *topicP = TOPICP; // topic for sending data
const char *topicD = TOPICD; // topic for debug

const int mqtt_timeout = 1;

// Pin definitions
#define echoPin 25
#define trigPin 26
#define relayPin 33

// Variables
bool pump_running = false;
bool pump_switch = false;
int pump_start_level = 0;
int dry_run_check_interval = 360;
int dry_run_check_counter = 0;
bool dry_run_wait = false;
int dry_run_wait_counter = 0;
int dry_run_wait_interval = 5400;

// Constants
const int max_range = 450;
const float speed_of_sound = 0.0343;
const int water_stop_distance = 25;
const int tank_height = 140;
const int water_level_low = 100;

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (80)
char msg[MSG_BUFFER_SIZE];
int value = 0;

// Function to fill a rectangle on the display
void fillRectangle(int x, int y, int width, int height, int color)
{
    display.fillRect(x, y, width, height, color);
    display.display();
}

// Function to display a status message on the display
void displayStatus(int x, int y, int width, int height, const char *message)
{
    fillRectangle(x, y, width, height, SSD1306_BLACK);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(x, y);
    display.println(message);
    display.display();
}

// Function to set up WiFi connection
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
    Serial.println("\nWiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    displayStatus(0, 16, 64, 8, "Wifi On");
}

// Function to reconnect WiFi
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
        displayStatus(0, 16, 64, 8, "Wifi Error");
    }
    Serial.println("\nWiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    displayStatus(0, 16, 64, 8, "Wifi On");
}

// Function to reconnect MQTT
void reconnectMqtt()
{
    while (!client.connected())
    {
        if (client.connect(clientID, mqttUser, mqttPassword))
        {
            Serial.println("MQTT connected");
            displayStatus(64, 16, 64, 8, "Mqtt On");
            client.subscribe(topicR);
        }
        else
        {
            displayStatus(64, 16, 64, 8, "Mqtt Error");
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

// Function to convert IP address to string
String ipToString(IPAddress ip)
{
    String s = "";
    for (int i = 0; i < 4; i++)
        s += i ? "." + String(ip[i]) : String(ip[i]);
    return s;
}

// Function to send RSSI information
void sendRSSIInfo()
{
    String ip = ipToString(WiFi.localIP());
    String temp = "{\"RSSI\":" + String(WiFi.RSSI()) + ",\"IP\":\"" + ip + "\",\"Device\":\"ESP32\"}";
    Serial.println(temp);
    char tab2[1024];
    strncpy(tab2, temp.c_str(), sizeof(tab2));
    tab2[sizeof(tab2) - 1] = 0;
    client.publish(topicD, tab2);
}

// Function to get distance from ultrasonic sensor
long getDistance()
{
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(20);
    digitalWrite(trigPin, LOW);
    long duration = pulseIn(echoPin, HIGH, 26000);
    int distance = duration * speed_of_sound / 2;
    Serial.println(" Distance: " + String(distance) + " cm");
    return distance;
}

// Function to get water level
int getWaterLevel(int distance)
{
    return tank_height - distance;
}

// Function to get water level in percentage
int getWaterLevelInPercentage(int level)
{
    int per = (level + water_stop_distance) * 100 / tank_height;
    return (per <= 100) ? per : 100;
}

// Function to check if the tank is full
bool isTankFull(int level)
{
    return level >= 100;
}

// Function to stop the pump
void stopPump()
{
    Serial.println(" Stopping Pump");
    delay(15000);
    digitalWrite(relayPin, LOW);
    dry_run_wait = false;
    pump_running = false;
    dry_run_check_counter = 0;
    pump_switch = false;
    Serial.println(" Pump Stopped");
    displayStatus(0, 24, 80, 8, "Pump Stopped");
}

// Function to start the pump
void startPump(int level)
{
    if (isTankFull(level))
    {
        Serial.println(" Tank is full");
        stopPump();
        return;
    }
    Serial.println(" Starting Pump");
    delay(2000);
    digitalWrite(relayPin, HIGH);
    pump_running = true;
    dry_run_check_counter = dry_run_check_interval;
    pump_start_level = level;
    pump_switch = true;
    Serial.println(" Pump Started");
    displayStatus(0, 24, 80, 8, "Pump Running");
    displayStatus(0, 0, 128, 15, ("Level " + String(level) + "%").c_str());
}

// MQTT callback function
void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    String message;
    for (int i = 0; i < length; i++)
    {
        message += (char)payload[i];
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

// Function to set up water level controller
void setUpWaterLevelController()
{
    int distance = getDistance();
    int level = getWaterLevel(distance);
    pump_start_level = getWaterLevelInPercentage(level);
    startPump(pump_start_level);
    Serial.println(" Maximum Range: " + String(max_range) + " cm");
}

// Function to pause the pump
void pausePump()
{
    Serial.println(" Pausing Pump");
    delay(2000);
    digitalWrite(relayPin, LOW);
    pump_running = false;
    dry_run_check_counter = 0;
    Serial.println(" Pump Paused");
    displayStatus(0, 24, 80, 8, "Pump Paused");
}

// Function to check if water is flowing
bool isWaterFlowing(int level)
{
    return level > pump_start_level;
}

// Function to process dry run protection
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
            pausePump();
            dry_run_wait = true;
            dry_run_wait_counter = dry_run_wait_interval;
        }
    }
}

// Function to get pump status
String pumpStatus()
{
    if (pump_running)
    {
        Serial.println(" Pump is running ");
        displayStatus(0, 24, 80, 8, "Pump Running");
        return "Running";
    }
    else if (dry_run_wait)
    {
        Serial.println(" Pump is waiting for dry run ");
        displayStatus(0, 24, 80, 8, "Pump Paused");
        return "Paused";
    }
    else
    {
        Serial.println(" Pump is not running ");
        displayStatus(0, 24, 80, 8, "Pump Stopped");
        return "Stopped";
    }
}

// Function to control water level
void waterLevelController()
{
    int distance = getDistance();
    int level = getWaterLevel(distance);
    int per = getWaterLevelInPercentage(level);

    if (distance > max_range)
    {
        Serial.println(" Tank Height is more than supported range of " + String(max_range) + " cm");
        stopPump();
    }
    else if (level < 0)
    {
        Serial.println(" Sensor distance is more than the tank capacity");
        stopPump();
    }
    else
    {
        if (distance <= water_stop_distance && pump_running)
        {
            stopPump();
        }
        else if (distance > water_level_low && !pump_running && !dry_run_wait)
        {
            pump_start_level = level;
            startPump(per);
        }

        Serial.print(" Water level is " + String(per) + "%");
        displayStatus(0, 0, 128, 15, ("Level " + String(per) + "%").c_str());

        if (pump_switch)
        {
            processDryRunProtect(per);
        }

        String status = pumpStatus();
        String temp = "{\"Level\":" + String(per) + ",\"Distance\":" + String(distance) + ",\"PumpStatus\":\"" + status + "\",\"DryRunWait\":" + String(dry_run_wait) + ",\"MaxRange\":" + String(max_range) + ",\"WaterStopDistance\":" + String(water_stop_distance) + ",\"TankHeight\":" + String(tank_height) + ",\"LowWaterLevel\":" + String(water_level_low) + ",\"Unit\":\"CM\"}";
        Serial.println(temp);
        char tab2[1024];
        strncpy(tab2, temp.c_str(), sizeof(tab2));
        tab2[sizeof(tab2) - 1] = 0;
        client.publish(topicP, tab2);
    }
}

// Setup function
void setup()
{
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    pinMode(relayPin, OUTPUT);
    Serial.begin(115200);

    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
    {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;)
            ;
    }

    display.clearDisplay();
    display.display();

    Serial.println(" Ultrasonic Sensor HC-SR04");
    Serial.println(" with ESP8266 and mqtt PubSubClient");
    Serial.println(" Initializing... ");
    for (int i = 5; i > 0; i--)
    {
        Serial.println(i);
        delay(1000);
    }
    Serial.println(" Initialization complete");

    displayStatus(0, 16, 64, 8, "Wifi Off");
    displayStatus(64, 16, 64, 8, "Mqtt Off");

    setup_wifi();
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());

    client.setServer(mqttServer, 1883);
    client.setCallback(callback);
}

// Loop function
void loop()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        displayStatus(0, 16, 64, 8, "Wifi Off");
        reconnectWifi();
    }
    else
    {
        displayStatus(0, 16, 64, 8, "Wifi On");
        if (!client.connected())
        {
            reconnectMqtt();
        }
        client.loop();
    }

    waterLevelController();
    sendRSSIInfo();
    delay(1000);
}
