/*
 * Project myProject
 * Author: Your Name
 * Date:
 * For comprehensive documentation and examples, please visit:
 * https://docs.particle.io/firmware/best-practices/firmware-template/
 */

// Include Particle Device OS APIs
#include "Particle.h"
#include <Wire.h>

#include "SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h"

// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(AUTOMATIC);

// Run the application and system concurrently in separate threads
#ifndef SYSTEM_VERSION_v620
SYSTEM_THREAD(ENABLED); // System thread defaults to on in 6.2.0 and later and this line is not required
#endif

// EEPROM locations to store 4-byte variables
#define EEPROM_SIZE 100               // Allocate 100 bytes of EEPROM
#define LOCATION_CALIBRATION_FACTOR 0 // Float, requires 4 bytes of EEPROM
#define LOCATION_ZERO_OFFSET 10       // Must be more than 4 away from previous spot. int32_t, requires 4 bytes of EEPROM

void readSystemSettings(void);
void recordSystemSettings(void);
void calibrateScale(void);
void tareScale(void);

bool settingsDetected = false; // Used to prompt user to calibrate their scale
bool shouldCalibrate = false;  // Set to true when user wants to calibrate the scale
bool shouldTare = false;       // Set to true when user wants to tare the scale
int weightOnScale = 500;       // User defined known weight on scale for calibration (in lbs or kgs as preferred)

// Create an array to take average of weights. This helps smooth out jitter.
#define AVG_SIZE 4
float avgWeights[AVG_SIZE];
byte avgWeightSpot = 0;

// Show system, cloud connectivity, and application logs over USB
// View logs with CLI using 'particle serial monitor --follow'
SerialLogHandler logHandler(LOG_LEVEL_INFO);

NAU7802 myScale; // Create instance of the NAU7802 class

int calibrate(String command)
{
  shouldCalibrate = true;
  weightOnScale = command.toInt();
  return 1;
}

int tare(String command)
{
  shouldTare = true;
  return 1;
}

// setup() runs once, when the device is first turned on
void setup()
{
  // Put initialization like pinMode and begin functions here
  SystemPowerConfiguration powerConfig = System.getPowerConfiguration();
  powerConfig.auxiliaryPowerControlPin(D7).interruptPin(A7);
  System.setPowerConfiguration(powerConfig);

  Wire.begin();

  if (myScale.begin() == false)
  {
    Serial.println("Scale not detected. Please check wiring. Freezing...");
    while (1)
      ;
  }
  Particle.function("calibrate", calibrate);
  Particle.function("tare", tare);
  readSystemSettings();
}

// loop() runs over and over again, as quickly as it can execute.
void loop()
{
  // The core of your code will likely live here.

  if (myScale.available() == true)
  {
    int32_t currentReading = myScale.getReading();
    float currentWeight = myScale.getWeight();

    avgWeights[avgWeightSpot++] = currentWeight;
    if (avgWeightSpot == AVG_SIZE)
      avgWeightSpot = 0;

    float avgWeight = 0;
    for (int x = 0; x < AVG_SIZE; x++)
      avgWeight += avgWeights[x];
    avgWeight /= AVG_SIZE;

    Log.info("Reading: %ld Weight: %.2f Avg Weight: %.2f", currentReading, currentWeight, avgWeight);

    if (settingsDetected == false)
    {
      Log.info("Scale not calibrated...");
    }
    if (shouldCalibrate == true)
    {
      calibrateScale();
      shouldCalibrate = false;
    }
    if (shouldTare == true)
    {
      tareScale();
      shouldTare = false;
    }
  }
  delay(1500);
}

void tareScale()
{
  myScale.calculateZeroOffset(64); // Zero or Tare the scale. Average over 64 readings.
  Log.info("New zero offset: %ld", myScale.getZeroOffset());

  recordSystemSettings(); // Commit these values to EEPROM
}

// Gives user the ability to set a known weight on the scale and calculate a calibration factor
void calibrateScale()
{
  Log.info("Scale calibration");

  myScale.calculateCalibrationFactor(weightOnScale, 64); // Tell the library how much weight is currently on it
  Log.info("New cal factor: %.2f", myScale.getCalibrationFactor());
  Log.info("New Scale Reading: %.2f", myScale.getWeight());

  recordSystemSettings(); // Commit these values to EEPROM
}

// Record the current system settings to EEPROM
void recordSystemSettings(void)
{
  // Get various values from the library and commit them to NVM
  EEPROM.put(LOCATION_CALIBRATION_FACTOR, myScale.getCalibrationFactor());
  EEPROM.put(LOCATION_ZERO_OFFSET, myScale.getZeroOffset());
}

// Reads the current system settings from EEPROM
// If anything looks weird, reset setting to default value
void readSystemSettings(void)
{
  float settingCalibrationFactor; // Value used to convert the load cell reading to lbs or kg
  uint32_t settingZeroOffset;     // Zero value that is found when scale is tared

  // Look up the calibration factor
  EEPROM.get(LOCATION_CALIBRATION_FACTOR, settingCalibrationFactor);
  if (settingCalibrationFactor == 0xFFFFFFFF)
  {
    settingCalibrationFactor = 1.0; // Default to 1.0
    EEPROM.put(LOCATION_CALIBRATION_FACTOR, settingCalibrationFactor);
  }

  // Look up the zero tare point
  EEPROM.get(LOCATION_ZERO_OFFSET, settingZeroOffset);
  if (settingZeroOffset == 0xFFFFFFFF)
  {
    settingZeroOffset = 0; // Default to 0 - i.e. no offset
    EEPROM.put(LOCATION_ZERO_OFFSET, settingZeroOffset);
  }

  // Pass these values to the library
  myScale.setCalibrationFactor(settingCalibrationFactor);
  myScale.setZeroOffset(settingZeroOffset);

  settingsDetected = true; // Assume for the moment that there are good cal values
  if (settingCalibrationFactor == 1.0 || settingZeroOffset == 0)
    settingsDetected = false; // Defaults detected. Prompt user to cal scale.
}