#include <SoftwareSerial.h>
#include <SPI.h>
#include <SD.h>
#include <ThreeWire.h>  
#include <RtcDS1302.h>
#include "DHT.h"

// RTC setup
ThreeWire myWire(3, 7, 4);
RtcDS1302<ThreeWire> Rtc(myWire);

// DHT22 setup
#define DHTPIN 6
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Reed switch rainfall sensor setup
const int reedPin = 2;  // Pin where reed switch is connected
volatile int tipCount = 0;  // Counter for bucket tips
const float mmPerTip = 0.2;  // Calibration value: mm of rain per tip

// SD card setup
const int chipSelect = 10;
bool sdCardWorking = false;

// Rainfall tracking
int lastDay = -1;
static float dailyRainfall = 0;
static int lastTipCount = 0;

// Timing variables
unsigned long lastLogTime = 0;
const unsigned long logInterval = 600000;  // Log data every 1 minute (60000 ms)

// Interrupt for rain gauge
void rainInterrupt() {
    // Debounce using simple time delay
    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();
    
    // If interrupts come in faster than 50ms, assume it's bounce and ignore
    if (interruptTime - lastInterruptTime > 50) {
        tipCount++;
    }
    lastInterruptTime = interruptTime;
}

void setup() {
    // Initialize Serial communication
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for serial port to connect
    }
    
    Serial.println("Weather Station Initializing...");
    
    // Initialize sensors
    dht.begin();
    Serial.println("DHT22 sensor initialized.");
    
    // Reed switch setup with pullup and interrupt
    pinMode(reedPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(reedPin), rainInterrupt, FALLING);
    Serial.println("Rainfall sensor (reed switch) initialized.");
    
    // RTC initialization
    Rtc.Begin();

    // Set RTC to compilation time
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    
    // Check if RTC is running
    if (Rtc.GetIsRunning()) {
        Rtc.SetDateTime(compiled);
    }
    
    // Check if RTC time is valid
    if (!Rtc.IsDateTimeValid()) {
        Rtc.SetDateTime(compiled);
    }

    // Remove write protection
    if (Rtc.GetIsWriteProtected()) {
        Rtc.SetIsWriteProtected(false);
    }
    
    // Ensure RTC is running
    if (!Rtc.GetIsRunning()) {
        Rtc.SetIsRunning(true);
    }
    Serial.println("RTC module set.");
    
    // Initialize SD card - but continue if it fails
    sdCardWorking = initSDCard();
    
    // Take initial reading to ensure everything is working
    readAndDisplaySensors();
    
    Serial.println("Setup completed. Starting main loop...");
}

void loop() {
    // Get current time
    unsigned long currentMillis = millis();
    
    // Always read and display sensor data regardless of logging interval
    readAndDisplaySensors();
    
    // Log data to SD card at the specified interval
    if (currentMillis - lastLogTime >= logInterval && sdCardWorking) {
        logCurrentData();
        lastLogTime = currentMillis;
    }
    
    // Short delay to prevent flooding the serial monitor
    delay(1000);  // 1 second between readings
}

void readAndDisplaySensors() {
    // Get current time from RTC
    RtcDateTime now = Rtc.GetDateTime();
    String timestamp = getTimestamp(now);
    Serial.println("\n----- Sensor Readings -----");
    Serial.print("Time: ");
    Serial.println(timestamp);
    
    // Read temperature and humidity
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    // Check if readings are valid
    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("Failed to read from DHT sensor!");
    } else {
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println(" °C");
        
        Serial.print("Humidity: ");
        Serial.print(humidity);
        Serial.println(" %");
    }
    
    // Update and display rainfall
    updateRainfallData(now);
    Serial.print("Rainfall: ");
    Serial.print(dailyRainfall);
    Serial.println(" mm");
    Serial.println("---------------------------");
}

void updateRainfallData(const RtcDateTime& now) {
    int currentDay = now.Day();
    
    // Reset rainfall counter at midnight
    if (currentDay != lastDay && lastDay != -1) {
        Serial.println("New day detected, resetting daily rainfall counter.");
        lastTipCount = tipCount;
        dailyRainfall = 0;
        lastDay = currentDay;
    } else if (lastDay == -1) {
        lastDay = currentDay;
    }
    
    // Calculate new rainfall since last measurement
    int newTips = tipCount - lastTipCount;
    if (newTips > 0) {
        dailyRainfall += newTips * mmPerTip;
        lastTipCount = tipCount;
        Serial.print("New rain detected: ");
        Serial.print(newTips * mmPerTip);
        Serial.println(" mm");
    }
}

void logCurrentData() {
    RtcDateTime now = Rtc.GetDateTime();
    String timestamp = getTimestamp(now);
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    if (!isnan(temperature) && !isnan(humidity)) {
        if (logData(timestamp, temperature, humidity, dailyRainfall)) {
            Serial.println("Data logged to SD card.");
        } else {
            Serial.println("Failed to log data to SD card.");
            sdCardWorking = false;  // Mark SD card as not working
        }
    }
}

bool initSDCard() {
    Serial.print("Initializing SD card...");
    if (!SD.begin(chipSelect)) {
        Serial.println("Failed!");
        return false;
    }
    
    Serial.println("Success!");

    File dataFile = SD.open("SENSOR.CSV", FILE_WRITE);
    if (dataFile) {
        if (dataFile.size() == 0) {
            dataFile.println(F("Timestamp,Temperature(C),Humidity(%),Rainfall(mm)"));
        }
        dataFile.close();
        Serial.println("Data file ready");
    }
    
    Serial.println("Error opening datalog.csv");
    return false;
}

String getTimestamp(const RtcDateTime& dt) {
    char timestamp[20];
    snprintf_P(timestamp, sizeof(timestamp), PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
               dt.Month(), dt.Day(), dt.Year(), dt.Hour(), dt.Minute(), dt.Second());
    return String(timestamp);
}

bool logData(String timestamp, float temperature, float humidity, float rainfall) {
    File dataFile = SD.open("datalog.csv", FILE_WRITE);
    if (dataFile) {
        String dataString = timestamp + "," + 
                           String(temperature, 1) + "," + 
                           String(humidity, 1) + "," + 
                           String(rainfall, 1);
        
        dataFile.println(dataString);
        dataFile.close();
        return true;
    }
    return false;
}