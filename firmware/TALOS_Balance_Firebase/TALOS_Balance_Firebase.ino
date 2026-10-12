/*
  TALOS - ESP32 self-balancing robot with Firebase dashboard control

  Controller: NodeMCU ESP32 DevKit, ESP32-WROOM-32, 30-pin USB-C board
  IMU:        MPU6050
  Wheels:     2 x SM-S4315R continuous-rotation servos
  Legs:       2 x MG996R positional servos

  REQUIRED LIBRARIES
  - ESP32Servo by Kevin Harrington / John K. Bennett
  - ArduinoJson by Benoit Blanchon

  FIXED CONNECTIONS - DO NOT CHANGE UNLESS THE ROBOT IS REWIRED
  --------------------------------------------------------------------------
  ESP32 D21 / GPIO21 -> MPU6050 SDA
  ESP32 D22 / GPIO22 -> MPU6050 SCL
  ESP32 D25 / GPIO25 -> LEFT wheel servo signal
  ESP32 D26 / GPIO26 -> RIGHT wheel servo signal
  ESP32 D27 / GPIO27 -> LEFT MG996R leg servo signal
  ESP32 D33 / GPIO33 -> RIGHT MG996R leg servo signal

  MPU6050 VCC -> ESP32 3V3
  MPU6050 GND -> GND_COMMON

  Wheel servo red wires -> dedicated regulated wheel supply
  Each MG996R red wire  -> its own regulated LM2596 output
  Every converter OUT-, every servo ground, MPU6050 ground and ESP32 ground
  MUST join at GND_COMMON. Never power a servo from an ESP32 pin.

  CONTROL ARCHITECTURE
  --------------------------------------------------------------------------
  - A deterministic 200 Hz balance task runs on ESP32 Core 1.
  - Firebase/Wi-Fi runs separately on Core 0 and never drives servos directly.
  - The dashboard sends high-level commands, limits and posture requests.
  - Network loss cancels movement but leaves local stationary balancing active.
  - A fall or IMU fault stops the wheels and requires a new arm request.

  IMPORTANT LIMITATION
  Continuous-rotation servos have no wheel-speed feedback. The dashboard
  "wheel command" control changes lean/turn effort, not measured RPM. Exact
  speed or position control requires wheel encoders.

  FIRST TEST
  Put the robot on a stand with wheels clear of the floor. Keep leg assist OFF.
  Tune wheel neutral pulses, directions, angle signs, KP and KD before enabling
  movement or leg assist.
*/

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// 1. FIXED GPIO PLAN
// ============================================================================
constexpr uint8_t PIN_SDA = 21;
constexpr uint8_t PIN_SCL = 22;
constexpr uint8_t PIN_WHEEL_LEFT = 25;
constexpr uint8_t PIN_WHEEL_RIGHT = 26;
constexpr uint8_t PIN_LEG_LEFT = 27;
constexpr uint8_t PIN_LEG_RIGHT = 33;

// ============================================================================
// 2. WI-FI AND FIREBASE
// Replace only the two Wi-Fi placeholders before uploading.
// The database rules in the TALOS prototype allow public control reads and
// public status writes. Tighten these rules and add device authentication
// before using the robot outside a private demonstration network.
// ============================================================================
constexpr bool WIFI_FIREBASE_ENABLED = true;
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char *FIREBASE_DATABASE_URL =
  "https://esp32-self-balancing-robot-default-rtdb.asia-southeast1.firebasedatabase.app";

constexpr uint32_t FIREBASE_CONTROL_POLL_MS = 250;
constexpr uint32_t FIREBASE_STATUS_PUSH_MS = 500;
constexpr uint32_t NETWORK_MOVEMENT_TIMEOUT_MS = 2000;
constexpr uint32_t WIFI_RETRY_MS = 5000;

// ============================================================================
// 3. BALANCE TUNING - TUNE IN THIS ORDER
// wheel neutral -> directions/signs -> balance offset -> KP -> KD -> tiny KI
// ============================================================================
float KP = 18.0f;
float KI = 0.0f;     // Keep zero until KP and KD can balance the robot.
float KD = 0.65f;    // Derivative uses measured gyro rate.

float BALANCE_ANGLE_OFFSET_DEG = 0.0f;
constexpr float IMU_ANGLE_SIGN = 1.0f;
constexpr float IMU_GYRO_SIGN = 1.0f;

// Each continuous servo needs an individually measured stop pulse.
int WHEEL_LEFT_NEUTRAL_US = 1500;
int WHEEL_RIGHT_NEUTRAL_US = 1500;
constexpr int WHEEL_LEFT_BALANCE_DIR = 1;
constexpr int WHEEL_RIGHT_BALANCE_DIR = -1;
constexpr int WHEEL_LEFT_TURN_DIR = 1;
constexpr int WHEEL_RIGHT_TURN_DIR = 1;

constexpr int WHEEL_MIN_US = 1000;
constexpr int WHEEL_MAX_US = 2000;
constexpr int WHEEL_MAX_BALANCE_OFFSET_US = 330;
constexpr int WHEEL_MAX_TURN_OFFSET_US = 110;
constexpr int WHEEL_DEADBAND_US = 8;
constexpr float WHEEL_SLEW_US_PER_SECOND = 3200.0f;

// Dashboard speed maps to requested lean and turn effort, not measured RPM.
constexpr float MAX_MANUAL_LEAN_DEG = 3.5f;
constexpr int FORWARD_COMMAND_SIGN = 1;
constexpr int TURN_COMMAND_SIGN = 1;

// ============================================================================
// 4. LEG SERVO TUNING
// Set each center pulse with the mechanical linkage disconnected first.
// The direction values make mirrored servos bend symmetrically.
// ============================================================================
int LEG_LEFT_CENTER_US = 1500;
int LEG_RIGHT_CENTER_US = 1500;
constexpr int LEG_LEFT_BEND_DIR = 1;
constexpr int LEG_RIGHT_BEND_DIR = -1;
constexpr int LEG_MIN_US = 900;
constexpr int LEG_MAX_US = 2100;
constexpr int LEG_MAX_MANUAL_DELTA_US = 220;
constexpr int LEG_MAX_ASSIST_DELTA_US = 60;
constexpr float LEG_ASSIST_GAIN_US_PER_DEG = 5.0f;
constexpr float LEG_ASSIST_GYRO_US_PER_DPS = 0.20f;
constexpr float LEG_ASSIST_WINDOW_DEG = 12.0f;
constexpr float LEG_MIN_SLEW_US_PER_SECOND = 80.0f;
constexpr float LEG_MAX_SLEW_US_PER_SECOND = 1000.0f;
constexpr bool ALLOW_LEG_CONTROL_WHILE_DISARMED = true;

// ============================================================================
// 5. SAFETY AND FILTER SETTINGS
// ============================================================================
constexpr float ARM_ANGLE_LIMIT_DEG = 7.0f;
constexpr float MAX_TILT_ANGLE_DEG = 35.0f;
constexpr uint32_t FALL_DEBOUNCE_MS = 80;
constexpr uint32_t STARTUP_DELAY_MS = 3000;
constexpr uint16_t CONTROL_LOOP_HZ = 200;
constexpr uint16_t TELEMETRY_PRINT_MS = 100;

constexpr float PID_INTEGRAL_LIMIT = 45.0f;
constexpr float KALMAN_Q_ANGLE = 0.001f;
constexpr float KALMAN_Q_BIAS = 0.003f;
constexpr float KALMAN_R_MEASURE = 0.03f;
constexpr float ACCEL_VALID_MIN_G = 0.75f;
constexpr float ACCEL_VALID_MAX_G = 1.25f;

// ============================================================================
// 6. MPU6050 REGISTERS
// ============================================================================
constexpr uint8_t MPU6050_ADDRESS = 0x68;
constexpr uint8_t MPU_REG_SAMPLE_RATE = 0x19;
constexpr uint8_t MPU_REG_CONFIG = 0x1A;
constexpr uint8_t MPU_REG_GYRO_CONFIG = 0x1B;
constexpr uint8_t MPU_REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t MPU_REG_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t MPU_REG_POWER = 0x6B;
constexpr float MPU_ACCEL_SCALE = 16384.0f;  // +/-2 g
constexpr float MPU_GYRO_SCALE = 131.0f;     // +/-250 deg/s

enum RobotState : uint8_t {
  ROBOT_STARTUP,
  ROBOT_DISARMED,
  ROBOT_ARMED,
  ROBOT_FALLEN,
  ROBOT_IMU_FAULT
};

enum MotionCommand : uint8_t {
  COMMAND_STOP,
  COMMAND_FORWARD,
  COMMAND_BACKWARD,
  COMMAND_LEFT,
  COMMAND_RIGHT
};

enum ControlMode : uint8_t {
  MODE_MANUAL,
  MODE_BALANCE_ONLY,
  MODE_CAMERA_ASSIST
};

struct RemoteControl {
  MotionCommand command = COMMAND_STOP;
  ControlMode mode = MODE_MANUAL;
  int speedPercent = 40;
  int legPositionPercent = 25;
  int legSpeedPercent = 45;
  bool legAssist = false;
  bool armed = false;
};

struct Telemetry {
  RobotState state = ROBOT_STARTUP;
  float angleDeg = 0.0f;
  float gyroDps = 0.0f;
  float accelMagnitudeG = 1.0f;
  float pidOutputUs = 0.0f;
  int wheelLeftUs = 1500;
  int wheelRightUs = 1500;
  int legLeftUs = 1500;
  int legRightUs = 1500;
  uint32_t loopTimeUs = 0;
  uint32_t imuErrors = 0;
  bool networkFresh = false;
};

Servo wheelLeftServo;
Servo wheelRightServo;
Servo legLeftServo;
Servo legRightServo;

portMUX_TYPE controlMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE telemetryMux = portMUX_INITIALIZER_UNLOCKED;
RemoteControl sharedControl;
Telemetry sharedTelemetry;

TaskHandle_t balanceTaskHandle = nullptr;
TaskHandle_t networkTaskHandle = nullptr;

volatile bool serialArmRequest = false;
volatile bool serialDisarmRequest = false;
volatile bool startupReady = false;
bool serialPlotterMode = false;

// State owned only by the high-priority balance task.
RobotState robotState = ROBOT_STARTUP;
float pidIntegral = 0.0f;
float kalmanAngle = 0.0f;
float kalmanBias = 0.0f;
float kalmanCovariance[2][2] = {{0.0f, 0.0f}, {0.0f, 0.0f}};
float gyroYOffset = 0.0f;
float currentBalanceOutputUs = 0.0f;
float currentTurnOutputUs = 0.0f;
float currentLegLeftUs = 1500.0f;
float currentLegRightUs = 1500.0f;
uint32_t totalImuErrors = 0;
volatile uint32_t lastSuccessfulControlMs = 0;

template <typename T>
T clampValue(T value, T low, T high) {
  return value < low ? low : (value > high ? high : value);
}

float moveTowards(float current, float target, float maximumStep) {
  if (target > current + maximumStep) return current + maximumStep;
  if (target < current - maximumStep) return current - maximumStep;
  return target;
}

RemoteControl getControlSnapshot() {
  portENTER_CRITICAL(&controlMux);
  RemoteControl copy = sharedControl;
  portEXIT_CRITICAL(&controlMux);
  return copy;
}

void setControlSnapshot(const RemoteControl &value) {
  portENTER_CRITICAL(&controlMux);
  sharedControl = value;
  portEXIT_CRITICAL(&controlMux);
}

Telemetry getTelemetrySnapshot() {
  portENTER_CRITICAL(&telemetryMux);
  Telemetry copy = sharedTelemetry;
  portEXIT_CRITICAL(&telemetryMux);
  return copy;
}

void setTelemetrySnapshot(const Telemetry &value) {
  portENTER_CRITICAL(&telemetryMux);
  sharedTelemetry = value;
  portEXIT_CRITICAL(&telemetryMux);
}

const char *robotStateName(RobotState state) {
  switch (state) {
    case ROBOT_ARMED: return "armed";
    case ROBOT_DISARMED: return "disarmed";
    case ROBOT_FALLEN: return "fallen";
    case ROBOT_IMU_FAULT: return "imu-fault";
    default: return "startup";
  }
}

const char *commandName(MotionCommand command) {
  switch (command) {
    case COMMAND_FORWARD: return "forward";
    case COMMAND_BACKWARD: return "backward";
    case COMMAND_LEFT: return "left";
    case COMMAND_RIGHT: return "right";
    default: return "stop";
  }
}

MotionCommand parseCommand(const char *value) {
  if (!value) return COMMAND_STOP;
  if (!strcmp(value, "forward")) return COMMAND_FORWARD;
  if (!strcmp(value, "backward")) return COMMAND_BACKWARD;
  if (!strcmp(value, "left")) return COMMAND_LEFT;
  if (!strcmp(value, "right")) return COMMAND_RIGHT;
  return COMMAND_STOP;
}

ControlMode parseMode(const char *value) {
  if (value && !strcmp(value, "balance-only")) return MODE_BALANCE_ONLY;
  if (value && !strcmp(value, "camera-assist")) return MODE_CAMERA_ASSIST;
  return MODE_MANUAL;
}

// ============================================================================
// SERVO OUTPUTS
// ============================================================================
void writeWheelPulses(int leftUs, int rightUs) {
  leftUs = clampValue(leftUs, WHEEL_MIN_US, WHEEL_MAX_US);
  rightUs = clampValue(rightUs, WHEEL_MIN_US, WHEEL_MAX_US);
  wheelLeftServo.writeMicroseconds(leftUs);
  wheelRightServo.writeMicroseconds(rightUs);
}

void stopWheels() {
  currentBalanceOutputUs = 0.0f;
  currentTurnOutputUs = 0.0f;
  writeWheelPulses(WHEEL_LEFT_NEUTRAL_US, WHEEL_RIGHT_NEUTRAL_US);
}

void writeLegPulses(int leftUs, int rightUs) {
  leftUs = clampValue(leftUs, LEG_MIN_US, LEG_MAX_US);
  rightUs = clampValue(rightUs, LEG_MIN_US, LEG_MAX_US);
  legLeftServo.writeMicroseconds(leftUs);
  legRightServo.writeMicroseconds(rightUs);
}

void centerLegs() {
  currentLegLeftUs = LEG_LEFT_CENTER_US;
  currentLegRightUs = LEG_RIGHT_CENTER_US;
  writeLegPulses(LEG_LEFT_CENTER_US, LEG_RIGHT_CENTER_US);
}

// ============================================================================
// MPU6050 DRIVER AND FILTER
// ============================================================================
bool mpuWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool mpuInitialize() {
  Wire.beginTransmission(MPU6050_ADDRESS);
  if (Wire.endTransmission() != 0) return false;

  return mpuWrite(MPU_REG_POWER, 0x01) &&
         mpuWrite(MPU_REG_CONFIG, 0x03) &&
         mpuWrite(MPU_REG_SAMPLE_RATE, 0x04) &&
         mpuWrite(MPU_REG_GYRO_CONFIG, 0x00) &&
         mpuWrite(MPU_REG_ACCEL_CONFIG, 0x00);
}

bool mpuRead(float &ax, float &ay, float &az,
             float &gx, float &gy, float &gz) {
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(MPU_REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;

  size_t received = Wire.requestFrom((uint8_t)MPU6050_ADDRESS, (size_t)14, true);
  if (received != 14 || Wire.available() < 14) {
    while (Wire.available()) Wire.read();
    return false;
  }

  uint8_t raw[14];
  for (uint8_t index = 0; index < 14; ++index) raw[index] = Wire.read();

  int16_t rawAx = (int16_t)((raw[0] << 8) | raw[1]);
  int16_t rawAy = (int16_t)((raw[2] << 8) | raw[3]);
  int16_t rawAz = (int16_t)((raw[4] << 8) | raw[5]);
  int16_t rawGx = (int16_t)((raw[8] << 8) | raw[9]);
  int16_t rawGy = (int16_t)((raw[10] << 8) | raw[11]);
  int16_t rawGz = (int16_t)((raw[12] << 8) | raw[13]);

  ax = rawAx / MPU_ACCEL_SCALE;
  ay = rawAy / MPU_ACCEL_SCALE;
  az = rawAz / MPU_ACCEL_SCALE;
  gx = rawGx / MPU_GYRO_SCALE;
  gy = rawGy / MPU_GYRO_SCALE;
  gz = rawGz / MPU_GYRO_SCALE;
  return true;
}

float kalmanUpdate(float accelerometerAngle, float gyroRate,
                   float accelerationMagnitude, float dt) {
  float correctedRate = gyroRate - kalmanBias;
  kalmanAngle += dt * correctedRate;

  kalmanCovariance[0][0] += dt * (dt * kalmanCovariance[1][1] -
                                  kalmanCovariance[0][1] -
                                  kalmanCovariance[1][0] + KALMAN_Q_ANGLE);
  kalmanCovariance[0][1] -= dt * kalmanCovariance[1][1];
  kalmanCovariance[1][0] -= dt * kalmanCovariance[1][1];
  kalmanCovariance[1][1] += KALMAN_Q_BIAS * dt;

  if (accelerationMagnitude < ACCEL_VALID_MIN_G ||
      accelerationMagnitude > ACCEL_VALID_MAX_G) {
    return kalmanAngle;
  }

  float noise = KALMAN_R_MEASURE *
                (1.0f + 8.0f * fabsf(accelerationMagnitude - 1.0f));
  float innovationCovariance = kalmanCovariance[0][0] + noise;
  float gain0 = kalmanCovariance[0][0] / innovationCovariance;
  float gain1 = kalmanCovariance[1][0] / innovationCovariance;
  float innovation = accelerometerAngle - kalmanAngle;

  kalmanAngle += gain0 * innovation;
  kalmanBias += gain1 * innovation;

  float p00 = kalmanCovariance[0][0];
  float p01 = kalmanCovariance[0][1];
  kalmanCovariance[0][0] -= gain0 * p00;
  kalmanCovariance[0][1] -= gain0 * p01;
  kalmanCovariance[1][0] -= gain1 * p00;
  kalmanCovariance[1][1] -= gain1 * p01;
  return kalmanAngle;
}

bool calibrateMPU() {
  constexpr uint16_t SAMPLE_COUNT = 600;
  float gyroYSum = 0.0f;
  float axSum = 0.0f;
  float aySum = 0.0f;
  float azSum = 0.0f;

  for (uint16_t sample = 0; sample < SAMPLE_COUNT; ++sample) {
    float ax, ay, az, gx, gy, gz;
    if (!mpuRead(ax, ay, az, gx, gy, gz)) return false;
    gyroYSum += gy;
    axSum += ax;
    aySum += ay;
    azSum += az;
    delay(3);
  }

  gyroYOffset = gyroYSum / SAMPLE_COUNT;
  float ax = axSum / SAMPLE_COUNT;
  float ay = aySum / SAMPLE_COUNT;
  float az = azSum / SAMPLE_COUNT;
  kalmanAngle = IMU_ANGLE_SIGN *
                atan2f(ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
  kalmanBias = 0.0f;
  memset(kalmanCovariance, 0, sizeof(kalmanCovariance));
  return true;
}

// ============================================================================
// BALANCE, MOVEMENT AND LEG CONTROL
// ============================================================================
void resetPID() {
  pidIntegral = 0.0f;
  currentBalanceOutputUs = 0.0f;
  currentTurnOutputUs = 0.0f;
}

float calculatePID(float targetAngle, float angle, float gyroRate, float dt) {
  float error = targetAngle - angle;
  float proportional = KP * error;
  float derivative = -KD * gyroRate;

  float proposedIntegral = clampValue(pidIntegral + error * dt,
                                      -PID_INTEGRAL_LIMIT,
                                      PID_INTEGRAL_LIMIT);
  float proposedOutput = proportional + KI * proposedIntegral + derivative;
  bool saturatingHigh = proposedOutput > WHEEL_MAX_BALANCE_OFFSET_US;
  bool saturatingLow = proposedOutput < -WHEEL_MAX_BALANCE_OFFSET_US;

  if ((!saturatingHigh && !saturatingLow) ||
      (saturatingHigh && error < 0.0f) ||
      (saturatingLow && error > 0.0f)) {
    pidIntegral = proposedIntegral;
  } else {
    pidIntegral *= 0.998f;
  }

  return proportional + KI * pidIntegral + derivative;
}

void calculateMovementTargets(const RemoteControl &control,
                              float &leanDeg, float &turnUs) {
  leanDeg = 0.0f;
  turnUs = 0.0f;
  if (control.mode == MODE_BALANCE_ONLY) return;

  float demand = clampValue(control.speedPercent, 0, 100) / 100.0f;
  switch (control.command) {
    case COMMAND_FORWARD:
      leanDeg = FORWARD_COMMAND_SIGN * MAX_MANUAL_LEAN_DEG * demand;
      break;
    case COMMAND_BACKWARD:
      leanDeg = -FORWARD_COMMAND_SIGN * MAX_MANUAL_LEAN_DEG * demand;
      break;
    case COMMAND_LEFT:
      turnUs = -TURN_COMMAND_SIGN * WHEEL_MAX_TURN_OFFSET_US * demand;
      break;
    case COMMAND_RIGHT:
      turnUs = TURN_COMMAND_SIGN * WHEEL_MAX_TURN_OFFSET_US * demand;
      break;
    default:
      break;
  }
}

void driveWheels(float requestedBalanceUs, float requestedTurnUs, float dt,
                 int &leftPulse, int &rightPulse) {
  requestedBalanceUs = clampValue(requestedBalanceUs,
                                  (float)-WHEEL_MAX_BALANCE_OFFSET_US,
                                  (float)WHEEL_MAX_BALANCE_OFFSET_US);
  requestedTurnUs = clampValue(requestedTurnUs,
                               (float)-WHEEL_MAX_TURN_OFFSET_US,
                               (float)WHEEL_MAX_TURN_OFFSET_US);
  if (fabsf(requestedBalanceUs) < WHEEL_DEADBAND_US) requestedBalanceUs = 0.0f;

  float maximumStep = WHEEL_SLEW_US_PER_SECOND * dt;
  currentBalanceOutputUs = moveTowards(currentBalanceOutputUs,
                                       requestedBalanceUs, maximumStep);
  currentTurnOutputUs = moveTowards(currentTurnOutputUs,
                                    requestedTurnUs, maximumStep);

  leftPulse = WHEEL_LEFT_NEUTRAL_US +
              WHEEL_LEFT_BALANCE_DIR * lroundf(currentBalanceOutputUs) +
              WHEEL_LEFT_TURN_DIR * lroundf(currentTurnOutputUs);
  rightPulse = WHEEL_RIGHT_NEUTRAL_US +
               WHEEL_RIGHT_BALANCE_DIR * lroundf(currentBalanceOutputUs) +
               WHEEL_RIGHT_TURN_DIR * lroundf(currentTurnOutputUs);
  leftPulse = clampValue(leftPulse, WHEEL_MIN_US, WHEEL_MAX_US);
  rightPulse = clampValue(rightPulse, WHEEL_MIN_US, WHEEL_MAX_US);
  writeWheelPulses(leftPulse, rightPulse);
}

void updateLegs(const RemoteControl &control, float angleError, float gyroRate,
                float dt, bool allowAssist, int &leftPulse, int &rightPulse) {
  float manualDelta = map(clampValue(control.legPositionPercent, 0, 100),
                          0, 100, 0, LEG_MAX_MANUAL_DELTA_US);
  float assistDelta = 0.0f;

  if (allowAssist && control.legAssist &&
      fabsf(angleError) <= LEG_ASSIST_WINDOW_DEG) {
    assistDelta = LEG_ASSIST_GAIN_US_PER_DEG * angleError -
                  LEG_ASSIST_GYRO_US_PER_DPS * gyroRate;
    assistDelta = clampValue(assistDelta,
                             (float)-LEG_MAX_ASSIST_DELTA_US,
                             (float)LEG_MAX_ASSIST_DELTA_US);
  }

  float combinedDelta = manualDelta + assistDelta;
  float targetLeft = LEG_LEFT_CENTER_US + LEG_LEFT_BEND_DIR * combinedDelta;
  float targetRight = LEG_RIGHT_CENTER_US + LEG_RIGHT_BEND_DIR * combinedDelta;
  targetLeft = clampValue(targetLeft, (float)LEG_MIN_US, (float)LEG_MAX_US);
  targetRight = clampValue(targetRight, (float)LEG_MIN_US, (float)LEG_MAX_US);

  float speedRatio = clampValue(control.legSpeedPercent, 0, 100) / 100.0f;
  float slew = LEG_MIN_SLEW_US_PER_SECOND +
               speedRatio * (LEG_MAX_SLEW_US_PER_SECOND - LEG_MIN_SLEW_US_PER_SECOND);
  float maximumStep = slew * dt;
  currentLegLeftUs = moveTowards(currentLegLeftUs, targetLeft, maximumStep);
  currentLegRightUs = moveTowards(currentLegRightUs, targetRight, maximumStep);

  leftPulse = lroundf(currentLegLeftUs);
  rightPulse = lroundf(currentLegRightUs);
  writeLegPulses(leftPulse, rightPulse);
}

void enterSafeState(RobotState newState) {
  robotState = newState;
  resetPID();
  stopWheels();
  if (newState == ROBOT_FALLEN || newState == ROBOT_IMU_FAULT) centerLegs();
}

// ============================================================================
// CORE 1 - HIGH-PRIORITY, FIXED-RATE BALANCE TASK
// A hardware ISR is intentionally not used: Wire/I2C and servo APIs are not
// safe inside an interrupt. vTaskDelayUntil provides stable 5 ms scheduling.
// ============================================================================
void balanceTask(void *parameter) {
  const TickType_t periodTicks = pdMS_TO_TICKS(1000 / CONTROL_LOOP_HZ);
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t previousMicros = micros();
  uint32_t fallStartedMs = 0;
  uint8_t consecutiveImuErrors = 0;
  bool previousRemoteArmed = false;
  bool localSerialArmed = false;

  for (;;) {
    uint32_t loopStartUs = micros();
    float dt = (loopStartUs - previousMicros) * 0.000001f;
    previousMicros = loopStartUs;
    dt = clampValue(dt, 0.002f, 0.012f);

    RemoteControl control = getControlSnapshot();
    bool networkFresh = !WIFI_FIREBASE_ENABLED ||
                        (millis() - lastSuccessfulControlMs <= NETWORK_MOVEMENT_TIMEOUT_MS);
    if (!networkFresh) control.command = COMMAND_STOP;

    float ax, ay, az, gx, gy, gz;
    if (!mpuRead(ax, ay, az, gx, gy, gz)) {
      ++totalImuErrors;
      if (++consecutiveImuErrors >= 3) enterSafeState(ROBOT_IMU_FAULT);
      stopWheels();
      vTaskDelayUntil(&lastWake, periodTicks);
      continue;
    }
    consecutiveImuErrors = 0;

    float accelMagnitude = sqrtf(ax * ax + ay * ay + az * az);
    float accelAngle = IMU_ANGLE_SIGN *
                       atan2f(ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
    float gyroRate = IMU_GYRO_SIGN * (gy - gyroYOffset);
    float angle = kalmanUpdate(accelAngle, gyroRate, accelMagnitude, dt);

    if (serialDisarmRequest) {
      serialDisarmRequest = false;
      localSerialArmed = false;
      if (robotState != ROBOT_IMU_FAULT) enterSafeState(ROBOT_DISARMED);
    }

    bool remoteArmRisingEdge = control.armed && !previousRemoteArmed;
    previousRemoteArmed = control.armed;
    if (serialArmRequest) {
      serialArmRequest = false;
      localSerialArmed = true;
    }

    bool effectiveArmed = control.armed || localSerialArmed;
    if (!effectiveArmed && robotState != ROBOT_IMU_FAULT) {
      enterSafeState(ROBOT_DISARMED);
    }

    if (localSerialArmed || remoteArmRisingEdge) {
      if (startupReady && robotState != ROBOT_IMU_FAULT &&
          robotState != ROBOT_ARMED &&
          fabsf(angle - BALANCE_ANGLE_OFFSET_DEG) <= ARM_ANGLE_LIMIT_DEG) {
        resetPID();
        robotState = ROBOT_ARMED;
      }
    }

    int wheelLeftUs = WHEEL_LEFT_NEUTRAL_US;
    int wheelRightUs = WHEEL_RIGHT_NEUTRAL_US;
    int legLeftUs = lroundf(currentLegLeftUs);
    int legRightUs = lroundf(currentLegRightUs);
    float pidOutput = 0.0f;

    if (robotState == ROBOT_ARMED) {
      float leanTarget, turnTarget;
      calculateMovementTargets(control, leanTarget, turnTarget);
      float targetAngle = BALANCE_ANGLE_OFFSET_DEG + leanTarget;

      if (fabsf(angle - BALANCE_ANGLE_OFFSET_DEG) > MAX_TILT_ANGLE_DEG) {
        if (fallStartedMs == 0) fallStartedMs = millis();
        if (millis() - fallStartedMs >= FALL_DEBOUNCE_MS) {
          enterSafeState(ROBOT_FALLEN);
          localSerialArmed = false;
        }
      } else {
        fallStartedMs = 0;
      }

      if (robotState == ROBOT_ARMED) {
        pidOutput = calculatePID(targetAngle, angle, gyroRate, dt);
        driveWheels(pidOutput, turnTarget, dt, wheelLeftUs, wheelRightUs);
        updateLegs(control, targetAngle - angle, gyroRate, dt, true,
                   legLeftUs, legRightUs);
      }
    } else {
      stopWheels();
      wheelLeftUs = WHEEL_LEFT_NEUTRAL_US;
      wheelRightUs = WHEEL_RIGHT_NEUTRAL_US;

      if (startupReady && ALLOW_LEG_CONTROL_WHILE_DISARMED &&
          robotState == ROBOT_DISARMED) {
        updateLegs(control, 0.0f, 0.0f, dt, false, legLeftUs, legRightUs);
      } else {
        centerLegs();
        legLeftUs = LEG_LEFT_CENTER_US;
        legRightUs = LEG_RIGHT_CENTER_US;
      }
    }

    Telemetry telemetry;
    telemetry.state = robotState;
    telemetry.angleDeg = angle;
    telemetry.gyroDps = gyroRate;
    telemetry.accelMagnitudeG = accelMagnitude;
    telemetry.pidOutputUs = pidOutput;
    telemetry.wheelLeftUs = wheelLeftUs;
    telemetry.wheelRightUs = wheelRightUs;
    telemetry.legLeftUs = legLeftUs;
    telemetry.legRightUs = legRightUs;
    telemetry.loopTimeUs = micros() - loopStartUs;
    telemetry.imuErrors = totalImuErrors;
    telemetry.networkFresh = networkFresh;
    setTelemetrySnapshot(telemetry);

    vTaskDelayUntil(&lastWake, periodTicks);
  }
}

// ============================================================================
// CORE 0 - WI-FI AND FIREBASE REST BRIDGE
// ============================================================================
String firebaseUrl(const char *path) {
  return String(FIREBASE_DATABASE_URL) + "/" + path + ".json";
}

bool connectWiFi(uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t startedMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedMs < timeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  return WiFi.status() == WL_CONNECTED;
}

int firebaseGet(const char *path, String &response) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  WiFiClientSecure client;
  client.setInsecure();  // Prototype only; use a trusted CA for production.
  HTTPClient http;
  http.setConnectTimeout(2000);
  http.setTimeout(2500);
  if (!http.begin(client, firebaseUrl(path))) return -2;
  int code = http.GET();
  response = http.getString();
  http.end();
  return code;
}

int firebasePatch(const char *path, const String &body) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  WiFiClientSecure client;
  client.setInsecure();  // Prototype only; use a trusted CA for production.
  HTTPClient http;
  http.setConnectTimeout(2000);
  http.setTimeout(2500);
  if (!http.begin(client, firebaseUrl(path))) return -2;
  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest("PATCH", body);
  http.end();
  return code;
}

bool readFirebaseControl() {
  String response;
  int code = firebaseGet("robot/control", response);
  if (code != HTTP_CODE_OK) return false;

  JsonDocument document;
  if (deserializeJson(document, response)) return false;

  RemoteControl next = getControlSnapshot();
  next.command = parseCommand(document["command"] | "stop");
  next.mode = parseMode(document["mode"] | "manual");
  next.speedPercent = clampValue((int)(document["speed"] | next.speedPercent), 0, 100);
  next.legPositionPercent = clampValue((int)(document["legPosition"] | next.legPositionPercent), 0, 100);
  next.legSpeedPercent = clampValue((int)(document["legSpeed"] | next.legSpeedPercent), 0, 100);
  next.legAssist = document["legAssist"] | false;
  next.armed = document["armed"] | false;
  setControlSnapshot(next);
  lastSuccessfulControlMs = millis();
  return true;
}

bool pushFirebaseStatus() {
  Telemetry telemetry = getTelemetrySnapshot();
  RemoteControl control = getControlSnapshot();

  JsonDocument document;
  document["online"] = true;
  document["state"] = robotStateName(telemetry.state);
  document["tilt"] = telemetry.angleDeg;
  document["gyroY"] = telemetry.gyroDps;
  document["pidOutput"] = telemetry.pidOutputUs;
  document["balancingEnabled"] = telemetry.state == ROBOT_ARMED;
  document["wifiRssi"] = WiFi.RSSI();
  document["lastCommand"] = commandName(control.command);
  document["wheelLeftUs"] = telemetry.wheelLeftUs;
  document["wheelRightUs"] = telemetry.wheelRightUs;
  document["legLeftUs"] = telemetry.legLeftUs;
  document["legRightUs"] = telemetry.legRightUs;
  document["loopTimeUs"] = telemetry.loopTimeUs;
  document["imuErrors"] = telemetry.imuErrors;
  document["networkFresh"] = telemetry.networkFresh;
  JsonObject timestamp = document["updatedAt"].to<JsonObject>();
  timestamp[".sv"] = "timestamp";

  String body;
  serializeJson(document, body);
  int code = firebasePatch("robot/status", body);
  return code >= 200 && code < 300;
}

void forceCloudSafeStartup() {
  JsonDocument document;
  document["armed"] = false;
  document["command"] = "stop";
  JsonObject timestamp = document["updatedAt"].to<JsonObject>();
  timestamp[".sv"] = "timestamp";
  String body;
  serializeJson(document, body);
  firebasePatch("robot/control", body);
}

void networkTask(void *parameter) {
  uint32_t lastPollMs = 0;
  uint32_t lastStatusMs = 0;
  uint32_t lastWiFiAttemptMs = 0;
  bool safeStartupPublished = false;

  for (;;) {
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (now - lastWiFiAttemptMs >= WIFI_RETRY_MS) {
        lastWiFiAttemptMs = now;
        connectWiFi(4000);
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (!safeStartupPublished) {
      forceCloudSafeStartup();
      safeStartupPublished = true;
    }

    if (now - lastPollMs >= FIREBASE_CONTROL_POLL_MS) {
      lastPollMs = now;
      readFirebaseControl();
    }

    if (now - lastStatusMs >= FIREBASE_STATUS_PUSH_MS) {
      lastStatusMs = now;
      pushFirebaseStatus();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ============================================================================
// SERIAL TESTING AND TELEMETRY
// ============================================================================
void printConnectionMap() {
  Serial.println();
  Serial.println(F("=== TALOS CONNECTION MAP ==="));
  Serial.println(F("D21/GPIO21 -> MPU6050 SDA"));
  Serial.println(F("D22/GPIO22 -> MPU6050 SCL"));
  Serial.println(F("D25/GPIO25 -> left wheel servo signal"));
  Serial.println(F("D26/GPIO26 -> right wheel servo signal"));
  Serial.println(F("D27/GPIO27 -> left MG996R signal"));
  Serial.println(F("D33/GPIO33 -> right MG996R signal"));
  Serial.println(F("MPU6050 VCC -> 3V3; every ground -> GND_COMMON"));
  Serial.println(F("Servo power comes from external converters, never ESP32."));
  Serial.println(F("Serial: A=arm, X=stop/disarm, P=plotter"));
  Serial.println(F("============================"));
}

void processSerialCommands() {
  while (Serial.available()) {
    char command = Serial.read();
    if (command == 'a' || command == 'A') serialArmRequest = true;
    if (command == 'x' || command == 'X') serialDisarmRequest = true;
    if (command == 'p' || command == 'P') serialPlotterMode = !serialPlotterMode;
  }
}

void printTelemetry() {
  Telemetry value = getTelemetrySnapshot();
  if (serialPlotterMode) {
    Serial.print(F("Angle:")); Serial.print(value.angleDeg, 3);
    Serial.print(F(" Gyro:")); Serial.print(value.gyroDps, 3);
    Serial.print(F(" PID:")); Serial.println(value.pidOutputUs, 2);
    return;
  }

  Serial.print(F("State=")); Serial.print(robotStateName(value.state));
  Serial.print(F(" angle=")); Serial.print(value.angleDeg, 2);
  Serial.print(F(" gyro=")); Serial.print(value.gyroDps, 2);
  Serial.print(F(" PID=")); Serial.print(value.pidOutputUs, 1);
  Serial.print(F(" wheels=")); Serial.print(value.wheelLeftUs);
  Serial.print('/'); Serial.print(value.wheelRightUs);
  Serial.print(F(" legs=")); Serial.print(value.legLeftUs);
  Serial.print('/'); Serial.print(value.legRightUs);
  Serial.print(F(" loop=")); Serial.print(value.loopTimeUs);
  Serial.print(F("us WiFi=")); Serial.print(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -100);
  Serial.print(F(" cloud=")); Serial.println(value.networkFresh ? F("fresh") : F("stale"));
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println(F("\n=== TALOS BALANCE + FIREBASE CONTROLLER ==="));
  printConnectionMap();

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(10);
  if (!mpuInitialize()) {
    Serial.println(F("FATAL: MPU6050 not detected. Check D21, D22, 3V3 and GND."));
    robotState = ROBOT_IMU_FAULT;
    while (true) delay(1000);
  }

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  wheelLeftServo.setPeriodHertz(50);
  wheelRightServo.setPeriodHertz(50);
  legLeftServo.setPeriodHertz(50);
  legRightServo.setPeriodHertz(50);
  wheelLeftServo.attach(PIN_WHEEL_LEFT, WHEEL_MIN_US, WHEEL_MAX_US);
  wheelRightServo.attach(PIN_WHEEL_RIGHT, WHEEL_MIN_US, WHEEL_MAX_US);
  legLeftServo.attach(PIN_LEG_LEFT, LEG_MIN_US, LEG_MAX_US);
  legRightServo.attach(PIN_LEG_RIGHT, LEG_MIN_US, LEG_MAX_US);
  stopWheels();
  centerLegs();

  Serial.println(F("Keep the robot upright and completely still: calibrating MPU6050..."));
  if (!calibrateMPU()) {
    Serial.println(F("FATAL: MPU6050 calibration read failed."));
    robotState = ROBOT_IMU_FAULT;
    stopWheels();
    centerLegs();
    while (true) delay(1000);
  }
  Serial.print(F("Gyro Y offset: "));
  Serial.println(gyroYOffset, 5);

  robotState = ROBOT_DISARMED;
  xTaskCreatePinnedToCore(balanceTask, "TALOS-Balance", 8192, nullptr, 4,
                         &balanceTaskHandle, 1);

  Serial.println(F("Three-second safety hold: wheels stopped, legs centered."));
  delay(STARTUP_DELAY_MS);
  startupReady = true;

  if (WIFI_FIREBASE_ENABLED) {
    xTaskCreatePinnedToCore(networkTask, "TALOS-Network", 12288, nullptr, 1,
                           &networkTaskHandle, 0);
  } else {
    lastSuccessfulControlMs = millis();
  }

  Serial.println(F("Ready. Test on a stand. Use A to arm and X to stop/disarm."));
}

void loop() {
  processSerialCommands();
  static uint32_t lastPrintMs = 0;
  if (millis() - lastPrintMs >= TELEMETRY_PRINT_MS) {
    lastPrintMs = millis();
    printTelemetry();
  }
  delay(2);
}
