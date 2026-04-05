
 * v2.1:
 * - Logic changed: the conveyor ALWAYS RUNS in RUNNING state.
 * - When a color is detected, the servo pushes the product, but the conveyor DOES NOT STOP.
 * - The system automatically goes back to color checking after sorting.
 * - START/STOP buttons are used to turn the whole system ON/OFF.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h> // Library for I2C LCD
#include <Servo.h>             // Library for Servo motor

// --- LCD setup ---
LiquidCrystal_I2C lcd(0x27, 16, 2); // Try 0x27 or 0x3F

// --- TCS3200 color sensor setup ---
#define S0 8
#define S1 9
#define S2 10
#define S3 11
#define sensorOut 12
int redValue = 0, greenValue = 0, blueValue = 0;
#define NUM_SAMPLES 5 // Number of samples to reduce noise

// --- Motor setup (L298N) ---
#define motorENA 5 // ENA pin (PWM - speed control)
#define motorIN1 4 // IN1 pin
#define motorIN2 7 // IN2 pin
#define CONVEYOR_SPEED 60 // Conveyor speed (0-255)

// --- Servo setup (MG995) ---
#define servoPin 3
Servo myServo;
#define ANGLE_NEUTRAL 90 // Waiting position
#define ANGLE_RED 45     // Position for Red
#define ANGLE_GREEN 135  // Position for Green
#define ANGLE_BLUE 180   // Position for Blue

// --- Button setup ---
#define BUTTON_START_PIN A0 // Start button
#define BUTTON_STOP_PIN A1  // Stop button

// --- COLOR DETECTION THRESHOLD (You need to adjust it) ---
// Smaller pulseIn value means that color is stronger.
// Open Serial Monitor to see average R, G, B values, then set threshold.
int threshold = 80;

// --- System states ---
#define STATE_STOPPED 0
#define STATE_RUNNING 1
int systemState = STATE_STOPPED; // At first, system is stopped

// LCD cache variables to reduce flickering
String lcdCacheL1 = "";
String lcdCacheL2 = "";

void setup() {
  Serial.begin(9600); // Start Serial for debugging

  // Start LCD
  lcd.init();
  lcd.backlight();
  updateLcd("HE THONG PHAN LOAI", "Dang khoi dong...");

  // Set color sensor pins
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(sensorOut, INPUT);
  digitalWrite(S0, HIGH);
  digitalWrite(S1, LOW); // Set scaling frequency to 20%

  // Set motor pins
  pinMode(motorENA, OUTPUT);
  pinMode(motorIN1, OUTPUT);
  pinMode(motorIN2, OUTPUT);

  // Set button pins (use internal pull-up resistor)
  pinMode(BUTTON_START_PIN, INPUT_PULLUP);
  pinMode(BUTTON_STOP_PIN, INPUT_PULLUP);

  // Set servo
  myServo.attach(servoPin);
  myServo.write(ANGLE_NEUTRAL); // Move servo to waiting position

  delay(1000); // Wait 1 second for startup
  
  // Stop conveyor at startup
  runConveyor(false, 0); 
  updateLcd("HE THONG: DUNG", "Nhan START de chay");
}

void loop() {
  // 1. Always check buttons
  checkButtons();

  // 2. Run sorting logic only when system is RUNNING
  if (systemState == STATE_RUNNING) {
    // 2.1. Read color values (with noise reduction)
    readColors_Averaging();

    // 2.2. Process color detection and sorting
    processColorAndSort();

    // Print values to Serial Monitor (for threshold adjustment)
    Serial.print("R: "); Serial.print(redValue);
    Serial.print(" | G: "); Serial.print(greenValue);
    Serial.print(" | B: "); Serial.println(blueValue);
    
    delay(50); // Wait 50ms
  } else {
    // If system is stopped, just wait
    delay(100);
  }
}

/**
 * @brief Update LCD display (reduce flickering)
 * Only print to LCD when new text is different from old text.
 * Add spaces to clear old characters.
 */
void updateLcd(String line1, String line2) {
  if (line1 != lcdCacheL1) {
    lcd.setCursor(0, 0);
    lcd.print(line1);
    for (int i = line1.length(); i < 16; i++) {
      lcd.print(" "); // Clear the rest of the line
    }
    lcdCacheL1 = line1;
  }
  if (line2 != lcdCacheL2) {
    lcd.setCursor(0, 1);
    lcd.print(line2);
    for (int i = line2.length(); i < 16; i++) {
      lcd.print(" "); // Clear the rest of the line
    }
    lcdCacheL2 = line2;
  }
}

/**
 * @brief Check START and STOP buttons.
 * Change system state when button is pressed.
 */
void checkButtons() {
  // Check START button (LOW because of INPUT_PULLUP)
  if (digitalRead(BUTTON_START_PIN) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BUTTON_START_PIN) == LOW && systemState == STATE_STOPPED) {
      // If system is stopped, start it
      systemState = STATE_RUNNING;
      runConveyor(true, CONVEYOR_SPEED);
      updateLcd("BANG CHUYEN: CHAY", "Dang doc mau...");
    }
  }

  // Check STOP button
  if (digitalRead(BUTTON_STOP_PIN) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BUTTON_STOP_PIN) == LOW && (systemState == STATE_RUNNING)) {
      // If system is running, stop it
      systemState = STATE_STOPPED;
      runConveyor(false, 0);
      myServo.write(ANGLE_NEUTRAL); // Move servo back to neutral
      updateLcd("HE THONG: DUNG", "Nhan START de chay");
    }
    // Note: If system is sorting now, STOP may not work
    // until sorting is finished. This is to avoid product jam.
  }
}

/**
 * @brief Read R, G, B values (using average)
 * Read NUM_SAMPLES times and calculate average to reduce noise.
 */
void readColors_Averaging() {
  long redSum = 0, greenSum = 0, blueSum = 0;
  long timeout = 100000; // 0.1 second

  for (int i = 0; i < NUM_SAMPLES; i++) {
    // Read Red color
    digitalWrite(S2, LOW); 
    digitalWrite(S3, LOW);
    redSum += pulseIn(sensorOut, LOW, timeout);
    delay(5); // Small delay

    // Read Green color
    digitalWrite(S2, HIGH); 
    digitalWrite(S3, HIGH);
    greenSum += pulseIn(sensorOut, LOW, timeout);
    delay(5);

    // Read Blue color
    digitalWrite(S2, LOW); 
    digitalWrite(S3, HIGH);
    blueSum += pulseIn(sensorOut, LOW, timeout);
    delay(5);
  }

  // Calculate average
  redValue = redSum / NUM_SAMPLES;
  greenValue = greenSum / NUM_SAMPLES;
  blueValue = blueSum / NUM_SAMPLES;
}

/**
 * @brief Control conveyor (run/stop only, no LCD update)
 */
void runConveyor(bool run, int speed) {
  if (run) {
    // Forward direction
    digitalWrite(motorIN1, HIGH);
    digitalWrite(motorIN2, LOW);
    analogWrite(motorENA, speed);
  } else {
    // Stop motor
    digitalWrite(motorIN1, LOW);
    digitalWrite(motorIN2, LOW);
    analogWrite(motorENA, 0);
  }
}

/**
 * @brief Main function: detect color and control servo.
 */
void processColorAndSort() {
  int detectedColor = 0; // 0=None, 1=Red, 2=Green, 3=Blue

  // Detection logic (smaller value = stronger color)
  // Add condition > 0 to avoid timeout case (pulseIn returns 0)

  // Check Red
  if (redValue < greenValue && redValue < blueValue && redValue < threshold && redValue > 0) {
    detectedColor = 1;
  }
  // Check Green
  else if (greenValue < redValue && greenValue < blueValue && greenValue < threshold && greenValue > 0) {
    detectedColor = 2;
  }
  // Check Blue
  else if (blueValue < redValue && blueValue < greenValue && blueValue < threshold && blueValue > 0) {
    detectedColor = 3;
  }

  // If one of the 3 colors is detected
  if (detectedColor > 0) {

    // 1. CHANGED: DO NOT STOP THE CONVEYOR
    // runConveyor(false, 0); // (This line was removed)

    // 2. Control servo and update LCD
    if (detectedColor == 1) {
      updateLcd("P.LOAI: DO", "Dang gat...");
      myServo.write(ANGLE_RED);
    } else if (detectedColor == 2) {
      updateLcd("P.LOAI: X. LA", "Dang gat...");
      myServo.write(ANGLE_GREEN);
    } else if (detectedColor == 3) {
      updateLcd("P.LOAI: X. DUONG", "Dang gat...");
      myServo.write(ANGLE_BLUE);
    }

    delay(1500); // Wait 1.5 seconds for servo to push product
                 // Note: During this 1.5s, conveyor is STILL RUNNING
                 // but sensor does not read color.
                 // Products should be placed far enough apart.

    // 4. Move servo back to waiting position
    myServo.write(ANGLE_NEUTRAL);
    delay(500); // Wait for servo to return

    // 5. CHANGED: remove all stop logic.
    // System will go back to loop()
    // and continue reading colors in STATE_RUNNING.
  }
  // If no color is detected, keep current state
  // and update LCD if needed
  else {
    updateLcd("BANG CHUYEN: CHAY", "Dang doc mau...");
  }
}
