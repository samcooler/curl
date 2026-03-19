#include <Arduino.h>
#include <PCA9539.h>

// PCA9539 I2C GPIO expander at address 0x77
PCA9539 pca9539(0x77);

const int NUM_SOLENOIDS = 12;
const int SOLENOID_ON_TIME = 500;   // ms each solenoid stays on
const int SOLENOID_OFF_TIME = 300;  // ms pause between solenoids

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Curl solenoid test starting");

  // Built-in LED for visual sanity check
  pinMode(LED_BUILTIN, OUTPUT);

  // Reset pin for PCA9539
  pinMode(D3, OUTPUT);
  digitalWrite(D3, HIGH);

  // Configure all 12 solenoid pins as outputs
  pca9539.pinMode(pca_A0, OUTPUT);
  pca9539.pinMode(pca_A1, OUTPUT);
  pca9539.pinMode(pca_A2, OUTPUT);
  pca9539.pinMode(pca_A3, OUTPUT);
  pca9539.pinMode(pca_A4, OUTPUT);
  pca9539.pinMode(pca_A5, OUTPUT);
  pca9539.pinMode(pca_A6, OUTPUT);
  pca9539.pinMode(pca_A7, OUTPUT);
  pca9539.pinMode(pca_B0, OUTPUT);
  pca9539.pinMode(pca_B1, OUTPUT);
  pca9539.pinMode(pca_B2, OUTPUT);
  pca9539.pinMode(pca_B3, OUTPUT);

  // All off initially
  for (int i = 0; i < NUM_SOLENOIDS; i++) {
    pca9539.digitalWrite(i, LOW);
  }

  Serial.println("Setup complete. Starting solenoid test cycle.");
}

void loop() {
  // Fire each solenoid one at a time
  for (int i = 0; i < NUM_SOLENOIDS; i++) {
    Serial.print("Solenoid ");
    Serial.print(i);
    Serial.println(" ON");

    pca9539.digitalWrite(i, HIGH);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(SOLENOID_ON_TIME);

    pca9539.digitalWrite(i, LOW);
    digitalWrite(LED_BUILTIN, LOW);
    Serial.print("Solenoid ");
    Serial.print(i);
    Serial.println(" OFF");

    delay(SOLENOID_OFF_TIME);
  }

  Serial.println("--- Cycle complete, restarting ---");
  delay(1000);
}