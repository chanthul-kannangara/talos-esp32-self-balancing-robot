#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <nvs.h>

#include "TalosWifiManager.h"

/*
  TALOS self-balancing firmware (HEIGHT-COORDINATED WHEEL + LEG STATE FEEDBACK)

  Fixed connections
  -----------------
  MPU6050 + LEFT AS5600: SDA GPIO21, SCL GPIO22
  RIGHT AS5600:          SDA GPIO16, SCL GPIO17
  RIGHT leg servo:       GPIO25
  LEFT leg servo:        GPIO26
  RIGHT wheel servo:     GPIO33
  LEFT wheel servo:      GPIO27
*/

// --------------------------- Pin plan ---------------------------------------
constexpr int MAIN_SDA_PIN = 21;
constexpr int MAIN_SCL_PIN = 22;
constexpr int RIGHT_ENCODER_SDA_PIN = 16;
constexpr int RIGHT_ENCODER_SCL_PIN = 17;

constexpr int RIGHT_LEG_PIN = 25;
constexpr int LEFT_LEG_PIN = 26;
constexpr int RIGHT_WHEEL_PIN = 33;
constexpr int LEFT_WHEEL_PIN = 27;
constexpr int STATUS_LED_PIN = 2;  // ESP32-WROOM built-in LED on most dev boards

constexpr uint8_t MPU6050_ADDRESS = 0x68;
constexpr uint8_t AS5600_ADDRESS = 0x36;

// ----------------------- Balance tuning ------------------------------------
float KP = 115.0f;         // aggressive angle correction for high COM
float KI = 0.0f;
float KD = 7.50f;          // stronger gyro-rate damping / earlier braking
float BALANCE_ANGLE = -1.0f;
float ROLL_BALANCE_ANGLE = 0.0f;
float BALANCE_TRIM_DEG = 0.0f;

int WHEEL_CONTROL_SIGN = -1;
// Runtime fall cutoff measured from the captured upright balance zero.
// Startup/re-arm angle limits below are intentionally separate.
float FALL_ANGLE_DEG = 60.0f;
constexpr float ARM_ANGLE_LIMIT_DEG = 8.0f;
constexpr float INTEGRAL_LIMIT = 20.0f;

constexpr uint32_t CONTROL_PERIOD_US = 2000;
constexpr float FILTER_TIME_CONSTANT_S = 0.30f;
constexpr uint32_t MAX_CONTROL_GAP_US = 30000;
constexpr uint32_t IMU_STALE_LIMIT_US = 20000;

constexpr float LARGE_ERROR_THRESHOLD_DEG = 0.35f;
constexpr float LARGE_ERROR_KP_GAIN = 1.35f;
constexpr float LARGE_ERROR_KD_GAIN = 1.20f;
constexpr float FULL_POWER_ERROR_DEG = 0.50f;
constexpr float FAST_TILT_ERROR_DEG = 0.10f;
constexpr float FAST_TILT_RATE_DPS = 3.5f;
constexpr float FAST_TILT_MIN_OUTPUT_RATIO = 0.94f;
// Predict where the body angle will be a few milliseconds ahead using gyro rate.
// 40 ms is an earlier but still bounded prediction for the tall/high-COM body.
float BALANCE_PREDICTION_TIME_S = 0.040f;
constexpr float RATE_FULL_POWER_DPS = 14.0f;
constexpr float QUIET_ERROR_DEG = 0.04f;
constexpr float QUIET_RATE_DPS = 1.2f;
constexpr float YAW_SPIN_FAULT_DPS = 250.0f;
constexpr float MIN_KP = 20.0f;
constexpr float MAX_KP = 250.0f;
constexpr float MIN_KI = 0.0f;
constexpr float MAX_KI = 20.0f;
constexpr float MIN_KD = 0.0f;
constexpr float MAX_KD = 30.0f;
constexpr float MIN_BALANCE_TRIM_DEG = -5.0f;
constexpr float MAX_BALANCE_TRIM_DEG = 5.0f;
constexpr float MIN_PREDICTION_TIME_S = 0.010f;
constexpr float MAX_PREDICTION_TIME_S = 0.080f;
constexpr int MIN_MAX_WHEEL_OUTPUT_US = 300;
constexpr int MAX_MAX_WHEEL_OUTPUT_US = 500;
constexpr int MIN_ACTIVE_WHEEL_OUTPUT_US = 100;
constexpr int MAX_ACTIVE_WHEEL_OUTPUT_US = 450;

// Dashboard movement remains a small setpoint adjustment around the local
// balance angle. Firebase never replaces or times the 500 Hz PID loop.
constexpr float MAX_DASHBOARD_LEAN_DEG = 0.18f;
constexpr int MAX_DASHBOARD_TURN_US = 80;
constexpr int DASHBOARD_FORWARD_SIGN = 1;
constexpr int DASHBOARD_TURN_SIGN = 1;

// ------------------------ Wheel calibration --------------------------------
constexpr int LEFT_WHEEL_NEUTRAL_US = 1500;
constexpr int RIGHT_WHEEL_NEUTRAL_US = 1500;
constexpr int LEFT_WHEEL_SIGN = -1;
constexpr int RIGHT_WHEEL_SIGN = 1;
int MAX_WHEEL_OUTPUT_US = 490;
constexpr int WHEEL_DEADBAND_US = 20;
int WHEEL_MIN_ACTIVE_US = 280;                    // earlier dead-zone breakaway
constexpr float WHEEL_ENGAGE_THRESHOLD = 1.0f;
// Four 500 Hz steps reach full authority, without a one-cycle max reversal.
constexpr int BALANCE_WHEEL_SLEW_US_PER_STEP = 140;
constexpr int WHEEL_SLEW_US_PER_STEP = 2000;  // <-- much faster ramp‑up

// ---------------- Height-based wheel gain scheduling -----------------------
constexpr float MIN_ROBOT_HEIGHT_CM = 16.2f;
constexpr float MAX_ROBOT_HEIGHT_CM = 29.2f;

// The physical wheel servos are rated at about 50 RPM. PWM is an open-loop
// speed request, so these values schedule authority rather than claiming a
// measured wheel RPM. A crouched robot receives gentler commands; a tall robot
// receives earlier, faster, and stronger correction.
constexpr float HEIGHT_KP_SCALE_BENT = 0.85f;
constexpr float HEIGHT_KP_SCALE_EXTENDED = 1.25f;
constexpr float HEIGHT_KD_SCALE_BENT = 0.95f;
constexpr float HEIGHT_KD_SCALE_EXTENDED = 1.20f;
constexpr float HEIGHT_OUTPUT_SCALE_BENT = 0.78f;
constexpr float HEIGHT_OUTPUT_SCALE_EXTENDED = 1.00f;
constexpr int HEIGHT_SLEW_BENT_US_PER_STEP = 90;
constexpr int HEIGHT_SLEW_EXTENDED_US_PER_STEP = 180;
constexpr int MIN_SCHEDULED_WHEEL_OUTPUT_US = 300;
constexpr float HEIGHT_FULL_POWER_ERROR_BENT_DEG = 1.10f;
constexpr float HEIGHT_FULL_POWER_ERROR_EXTENDED_DEG = 0.50f;
constexpr float HEIGHT_FAST_TILT_ERROR_BENT_DEG = 0.24f;
constexpr float HEIGHT_FAST_TILT_ERROR_EXTENDED_DEG = 0.10f;
constexpr float HEIGHT_FAST_OUTPUT_RATIO_BENT = 0.76f;
constexpr float HEIGHT_FAST_OUTPUT_RATIO_EXTENDED = 0.94f;

// ------------------------- Leg calibration ---------------------------------
constexpr int LEFT_LEG_NEUTRAL_US = 1500;
constexpr int RIGHT_LEG_NEUTRAL_US = 1500;

float leftStandTargetDeg = 307.8f;
float rightStandTargetDeg = 56.0f;

// External-encoder state feedback for continuous-rotation leg servos.
float LEG_KP = 18.0f;
float LEG_KI = 0.0f;
float LEG_KD = 0.55f;

constexpr int LEG_MIN_PULSE_US = 500;
constexpr int LEG_MAX_PULSE_US = 2500;
constexpr int LEG_MAX_OFFSET_US = 650;

// Maximum PWM offset from neutral (how much the motor can push)
constexpr int LEFT_LEG_MAX_SUPPORT_OFFSET_US = 180;
constexpr int RIGHT_LEG_MAX_SUPPORT_OFFSET_US = 320;
constexpr int LEG_MAX_RETRACT_OFFSET_US = 240;

// Servo PWM direction: positive controller output must support/extend each leg.
constexpr int LEFT_LEG_SUPPORT_SIGN = -1;
constexpr int RIGHT_LEG_SUPPORT_SIGN = 1;

constexpr int LEFT_LEG_ENCODER_SIGN = 1;
constexpr int RIGHT_LEG_ENCODER_SIGN = 1;

// Support starts only after encoder feedback detects sag. These side-specific
// values came from the latest safe bent-hold tests and are never applied as a
// permanent pulse at the target, which prevents slow full extension.
int LEFT_LEG_HOLD_BIAS_US = -60;
int RIGHT_LEG_HOLD_BIAS_US = 280;

// PID filter / deadband / slew
constexpr float LEG_ERROR_FILTER_ALPHA = 0.22f;
constexpr float LEG_POSITION_DEADBAND_DEG = 0.50f;
constexpr uint32_t LEG_PERIOD_US = 10000;
constexpr int LEG_SLEW_US_PER_STEP = 18;
constexpr int LEG_RECOVERY_SLEW_US_PER_STEP = 38;
constexpr int LEG_BRAKE_SLEW_US_PER_STEP = 60;
constexpr int LEG_NEUTRAL_RETURN_STEP_US = 250;

constexpr float LEG_HOLD_START_ERROR_DEG = 0.80f;
constexpr float LEG_HOLD_STOP_ERROR_DEG = 0.20f;
constexpr float LEG_RETRACT_START_ERROR_DEG = 0.65f;
constexpr float LEG_RETRACT_STOP_ERROR_DEG = 0.18f;
constexpr float LEG_STATE_PREDICTION_TIME_S = 0.035f;
constexpr float LEG_FULL_DRIVE_ERROR_DEG = 6.0f;
constexpr int LEG_RETRACT_MIN_ACTIVE_US = 75;

constexpr float MIN_LEG_MOTION_SPEED_CM_S = 0.50f;
constexpr float MAX_LEG_MOTION_SPEED_CM_S = 6.00f;
constexpr float MAX_DYNAMIC_CROUCH_EXTENSION = 0.08f;
constexpr float DYNAMIC_CROUCH_ENGAGE_PER_S = 0.80f;
constexpr float DYNAMIC_CROUCH_RELEASE_PER_S = 0.24f;
constexpr float DYNAMIC_CROUCH_START_ERROR_DEG = 0.40f;
constexpr float DYNAMIC_CROUCH_FULL_ERROR_DEG = 5.0f;
constexpr float DYNAMIC_CROUCH_START_RATE_DPS = 4.0f;
constexpr float DYNAMIC_CROUCH_FULL_RATE_DPS = 45.0f;

// Encoder-noise protection
constexpr uint16_t LEG_ENCODER_WARN_CYCLES = 10;
constexpr uint32_t LEG_ENCODER_STALE_LIMIT_US = 100000;

float leftLegFilteredErrorDeg = 0.0f;
float rightLegFilteredErrorDeg = 0.0f;
float leftLegPredictedErrorDeg = 0.0f;
float rightLegPredictedErrorDeg = 0.0f;
bool standPoseCaptured = false;

enum class LegDriveState : uint8_t {
  NEUTRAL,
  EXTEND,
  RETRACT
};

LegDriveState leftLegDriveState = LegDriveState::NEUTRAL;
LegDriveState rightLegDriveState = LegDriveState::NEUTRAL;

float requestedHeightExtension = 0.5f;
float commandedHeightExtension = 0.5f;
float dynamicCrouchExtension = 0.0f;
int legMotionSpeedPercent = 45;
bool heightTargetInitialized = false;

// Leg posture assistance (optional)
bool legAssistEnabled = false;
float ROLL_ASSIST_DEG_PER_BODY_DEG = 0.25f;
constexpr float ROLL_RATE_ASSIST_DEG_PER_DPS = 0.025f;
constexpr float ROLL_ASSIST_LIMIT_DEG = 4.0f;
constexpr int LEFT_ROLL_SIGN = 1;
constexpr int RIGHT_ROLL_SIGN = -1;

// -------------------------- Runtime objects --------------------------------
Servo leftWheel;
Servo rightWheel;
Servo leftLeg;
Servo rightLeg;
TwoWire rightEncoderBus = TwoWire(1);

constexpr uint32_t CALIBRATION_MAGIC = 0x54414C39;
constexpr uint32_t RUNTIME_SETTINGS_MAGIC = 0x54414C53;
constexpr uint16_t RUNTIME_SETTINGS_VERSION = 1;

struct StoredCalibration {
  uint32_t magic;
  float leftStand;
  float rightStand;
  float balance;
  float rollBalance;
  int32_t leftSign;   // unused now, kept for compatibility
  int32_t rightSign;
  int32_t leftHoldBiasUs;
  int32_t rightHoldBiasUs;
  float leftBentDeg;
  float rightBentDeg;
  float leftExtendedDeg;
  float rightExtendedDeg;
  int32_t bentCaptured;
  int32_t extendedCaptured;
};

struct StoredRuntimeSettings {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  float kp;
  float ki;
  float kd;
  float balanceTrimDeg;
  float predictionTimeS;
  int32_t maximumWheelOutputUs;
  int32_t minimumActiveWheelOutputUs;
  int32_t fallAngleDeg;
  int32_t reserved1;
  int32_t reserved2;
};

struct ImuSample {
  int16_t ax = 0, ay = 0, az = 0;
  int16_t gx = 0, gy = 0, gz = 0;
};

struct EncoderState {
  bool valid = false;
  bool magnetDetected = false;
  uint8_t magneticStatus = 0;
  uint16_t raw = 0;
  float angleDeg = 0.0f;
  float previousDeg = 0.0f;
  float velocityDps = 0.0f;
  uint32_t lastValidUs = 0;
  uint32_t lastReadUs = 0;
  bool initialized = false;
};

EncoderState leftEncoder;
EncoderState rightEncoder;

bool poseStreamEnabled = false;
uint32_t poseStreamDeadlineMs = 0;
uint32_t nextPoseStreamUs = 0;
uint32_t poseStreamSequence = 0;
uint32_t poseStreamBootId = 0;

float angleDeg = 0.0f;
float accelAngleDeg = 0.0f;
float gyroRateDps = 0.0f;
float gyroBiasDps = 0.0f;
float rollDeg = 0.0f;
float accelRollDeg = 0.0f;
float rollRateDps = 0.0f;
float rollRateBiasDps = 0.0f;
float yawRateDps = 0.0f;
float yawRateBiasDps = 0.0f;
float accelMagG = 1.0f;

float pidIntegral = 0.0f;
float pidOutput = 0.0f;
float predictedAngleDeg = 0.0f;

float leftBentDeg = 0.0f;
float rightBentDeg = 0.0f;
float leftExtendedDeg = 0.0f;
float rightExtendedDeg = 0.0f;
bool bentHeightCaptured = false;
bool extendedHeightCaptured = false;
bool heightCalibrationValid = false;

float currentExtension = 0.5f;
float currentRobotHeightCm = (MIN_ROBOT_HEIGHT_CM + MAX_ROBOT_HEIGHT_CM) * 0.5f;
float currentWheelKp = KP;
float currentWheelKd = KD;
int currentWheelMaxOutputUs = MAX_WHEEL_OUTPUT_US;
int currentWheelSlewUs = BALANCE_WHEEL_SLEW_US_PER_STEP;
float currentFullPowerErrorDeg = FULL_POWER_ERROR_DEG;
float currentFastTiltErrorDeg = FAST_TILT_ERROR_DEG;
float currentFastOutputRatio = FAST_TILT_MIN_OUTPUT_RATIO;
float currentWheelAuthorityPercent = 100.0f;

bool wheelManualTestActive = false;
int leftWheelManualCommandUs = 0;
int rightWheelManualCommandUs = 0;
uint32_t wheelManualUntilMs = 0;

int leftWheelPulse = LEFT_WHEEL_NEUTRAL_US;
int rightWheelPulse = RIGHT_WHEEL_NEUTRAL_US;
int leftLegPulse = LEFT_LEG_NEUTRAL_US;
int rightLegPulse = RIGHT_LEG_NEUTRAL_US;

bool armed = false;
bool standingEnabled = false;
bool sensorFault = false;
uint32_t startupCompleteMs = 0;
uint32_t nextControlUs = 0;
uint32_t nextLegUs = 0;
uint32_t nextTelemetryMs = 0;
uint32_t controlLoopCount = 0;
uint32_t controlLoopOverruns = 0;
uint32_t maximumControlLatenessUs = 0;
uint32_t lastImuSampleUs = 0;
uint32_t lastLegControlUs = 0;
uint32_t lastControlRunUs = 0;
uint32_t timingWindowUs = 0;
uint32_t timingWindowSamples = 0;
float measuredControlHz = 0.0f;
float leftLegIntegral = 0.0f;
float rightLegIntegral = 0.0f;
bool heightMotionPaused = false;
bool benchModeActive = false;
float leftRequestedJointDeg = 0.0f;
float rightRequestedJointDeg = 0.0f;
float leftActualTargetDeg = 0.0f;
float rightActualTargetDeg = 0.0f;
uint32_t benchMoveStartedMs = 0;
float benchInitialErrorDeg = 0.0f;
uint64_t lastJointCommandRevision = 0;
bool jointReplayGuardReady = false;
volatile bool jointMovePending = false;
float pendingLeftBendDeg = 0.0f;
float pendingRightBendDeg = 0.0f;
float jointSpeedDps = 6.0f;
char jointCommandState[32] = "idle";
char jointCommandMessage[112] = "Support body and enable leg-only hold.";
uint64_t jointCommandAckRevision = 0;
uint32_t imuErrors = 0;
uint16_t imuFaultStreak = 0;
constexpr uint16_t IMU_FAULT_STREAK_LIMIT = 50;
uint32_t encoderErrors = 0;
uint16_t legFaultStreak = 0;
bool legEncoderFaultActive = false;
String serialLine;

bool telemetryEnabled = true;
constexpr uint32_t TELEMETRY_INTERVAL_MS = 1000;

// ----------------------- Wi-Fi + Firebase bridge ----------------------------
const char *FIREBASE_DATABASE_URL =
    "https://esp32-self-balancing-robot-default-rtdb.asia-southeast1.firebasedatabase.app";
constexpr uint32_t FIREBASE_CONTROL_POLL_MS = 250;
constexpr uint32_t FIREBASE_STATUS_PUSH_MS = 1500;
constexpr uint32_t FIREBASE_POSE_PUSH_MS = 200;
// Firebase REST opens a TLS connection for each request. A control GET and a
// telemetry PATCH can legitimately take several seconds on a phone hotspot, so
// cloud freshness must never be used as a three-second balance kill switch.
// When this lease expires, remote motion is cancelled while the already-armed
// local balance loop remains in control.
constexpr uint32_t CLOUD_CONTROL_STALE_MS = 10000;
enum class DashboardMotion : uint8_t {
  STOP,
  FORWARD,
  BACKWARD,
  LEFT,
  RIGHT
};

enum class BalanceStopReason : uint8_t {
  NONE,
  DASHBOARD_OFF,
  TILT_LIMIT,
  YAW_LIMIT,
  IMU_FAULT,
  LEG_HOLD_FAULT,
  CONTROL_TIMING,
  MANUAL_STOP
};

enum class SettingsAck : uint8_t {
  NONE,
  APPLIED,
  INVALID,
  BUSY,
  STORAGE_ERROR
};

volatile bool serialControlActive = false;
volatile bool dashboardOffPending = false;
volatile bool localOffBaselineReady = false;
volatile uint64_t localOffBaselineRevision = 0;
volatile bool dashboardArmRequested = false;
volatile bool dashboardArmEdgePending = false;
volatile bool dashboardHasSeenOff = false;
volatile DashboardMotion dashboardMotion = DashboardMotion::STOP;
volatile int dashboardSpeedPercent = 40;
volatile uint64_t dashboardEmergencyStopNonce = 0;
volatile uint64_t dashboardSettingsRevision = 0;
volatile uint64_t dashboardHeightCommandRevision = 0;
volatile uint32_t lastSuccessfulControlMs = 0;
volatile int32_t lastControlHttpCode = 0;
volatile uint32_t lastControlLatencyMs = 0;
volatile uint32_t maxControlLatencyMs = 0;
volatile uint32_t consecutiveControlFailures = 0;
volatile int32_t lastStatusHttpCode = 0;
volatile uint32_t lastStatusLatencyMs = 0;
volatile bool cloudControlStale = true;
volatile bool motionRefreshRequired = true;
uint64_t dashboardMotionRevision = 0;
uint64_t motionRevisionAtLinkLoss = 0;
volatile BalanceStopReason lastBalanceStopReason = BalanceStopReason::NONE;
volatile float lastBalanceStopAngleDeg = 0.0f;
volatile uint32_t lastBalanceStopMs = 0;
volatile SettingsAck settingsAck = SettingsAck::NONE;
volatile uint64_t settingsAckRevision = 0;
uint32_t emergencyStopActiveUntilMs = 0;
uint64_t lastProcessedEmergencyStopNonce = 0;
uint64_t lastProcessedSettingsRevision = 0;
uint64_t lastProcessedHeightCommandRevision = 0;
bool heightCommandReplayGuardReady = false;
volatile uint64_t heightCommandAckRevision = 0;
char heightCommandState[32] = "idle";
char heightCommandMessage[112] = "No height command received.";
TalosWifiManager talosWifiManager;
char lastProcessedWifiCommandId[80] = "";
char deferredWifiReplayGuardId[80] = "";
bool deferredWifiReplayGuardPending = false;
char wifiCommandAckId[80] = "";
char wifiCommandState[32] = "idle";
char wifiCommandMessage[96] = "";
enum class PostureCommandAction : uint8_t {
  NONE,
  CAPTURE_BENT,
  CAPTURE_EXTENDED,
  BENCH_HOLD
};
volatile PostureCommandAction pendingPostureCommand = PostureCommandAction::NONE;
char lastProcessedPostureCommandId[80] = "";
char pendingPostureCommandId[80] = "";
char postureCommandAckId[80] = "";
char postureCommandState[32] = "idle";
char postureCommandMessage[112] = "No posture calibration requested.";
bool postureCommandReplayGuardReady = false;
TaskHandle_t firebaseTaskHandle = nullptr;
char pendingLocalWifiSsid[33] = {};
char pendingLocalWifiPassword[65] = {};
volatile bool pendingLocalWifiAdd = false;

// Scans, reconnects, AP mode changes, and NVS Wi-Fi writes are permitted only
// after every actuator has reached a mechanically safe off state. This callback
// is read by the Core 0 Wi-Fi task; it never changes actuator state itself.
bool talosWifiMutationSafe(void *context) {
  (void)context;
  const bool wheelsNeutral =
      leftWheelPulse == LEFT_WHEEL_NEUTRAL_US &&
      rightWheelPulse == RIGHT_WHEEL_NEUTRAL_US;
  const bool legsNeutral =
      leftLegPulse == LEFT_LEG_NEUTRAL_US &&
      rightLegPulse == RIGHT_LEG_NEUTRAL_US;
  return !armed && !dashboardArmRequested && !standingEnabled &&
         wheelsNeutral && legsNeutral;
}

// ---------------------- Guarded dashboard startup --------------------------
constexpr uint32_t AUTO_INITIAL_DELAY_MS = 1200;
constexpr uint32_t AUTO_CAPTURE_RETRY_MS = 1000;
constexpr uint32_t AUTO_LEG_SETTLE_MS = 2500;
constexpr uint32_t AUTO_ARM_COUNTDOWN_MS = 600;
constexpr uint32_t AUTO_REARM_STABLE_MS = 1200;
constexpr uint32_t AUTO_SENSOR_RETRY_MS = 1000;
constexpr uint32_t AUTO_POSE_RETRY_MS = 500;

// These limits are used only for automatic re-arming after a fall. They no
// longer block the initial power-on sequence.
constexpr float AUTO_MAX_STARTUP_PITCH_DEG = 15.0f;
constexpr float AUTO_MAX_GYRO_RATE_DPS = 4.0f;
constexpr float AUTO_MAX_YAW_RATE_DPS = 15.0f;
constexpr float AUTO_MAX_ACCEL_ERROR_G = 0.20f;

enum class AutoStartupState : uint8_t {
  WAIT_INITIAL_DELAY,
  CAPTURE_STAND_POSE,
  ENABLE_LEG_HOLD,
  WAIT_LEG_SETTLE,
  CAPTURE_BALANCE_ZERO,
  WAIT_BEFORE_ARM,
  RUNNING,
  WAIT_REARM,
  FAULT
};

AutoStartupState autoStartupState = AutoStartupState::WAIT_INITIAL_DELAY;
uint32_t autoStateSinceMs = 0;
uint32_t autoStableSinceMs = 0;

// ---------------------------- Utilities ------------------------------------
float wrap180(float angle) {
  while (angle > 180.0f) angle -= 360.0f;
  while (angle < -180.0f) angle += 360.0f;
  return angle;
}

int moveToward(int current, int target, int maximumStep) {
  if (target > current) return min(current + maximumStep, target);
  if (target < current) return max(current - maximumStep, target);
  return current;
}

float moveTowardFloat(float current, float target, float maximumStep) {
  if (target > current) return fminf(current + maximumStep, target);
  if (target < current) return fmaxf(current - maximumStep, target);
  return current;
}

float wrap360(float angle) {
  while (angle >= 360.0f) angle -= 360.0f;
  while (angle < 0.0f) angle += 360.0f;
  return angle;
}

float lerpFloat(float a, float b, float t) {
  return a + (b - a) * constrain(t, 0.0f, 1.0f);
}

bool validateHeightCalibration() {
  if (!bentHeightCaptured || !extendedHeightCaptured) return false;
  float leftTravel = wrap180(leftExtendedDeg - leftBentDeg);
  float rightTravel = wrap180(rightExtendedDeg - rightBentDeg);
  return isfinite(leftTravel) && isfinite(rightTravel) &&
      LEFT_LEG_ENCODER_SIGN * leftTravel >= 5.0f &&
      LEFT_LEG_ENCODER_SIGN * leftTravel <= 160.0f &&
      RIGHT_LEG_ENCODER_SIGN * rightTravel >= 5.0f &&
      RIGHT_LEG_ENCODER_SIGN * rightTravel <= 160.0f;
}

float normalizedLegExtension(float currentDeg, float bentDeg, float extendedDeg) {
  float totalTravel = wrap180(extendedDeg - bentDeg);
  if (fabsf(totalTravel) < 2.0f) return 0.5f;
  float currentTravel = wrap180(currentDeg - bentDeg);
  return constrain(currentTravel / totalTravel, 0.0f, 1.0f);
}

float calculateRobotExtension() {
  heightCalibrationValid = validateHeightCalibration();
  if (!heightCalibrationValid || !leftEncoder.valid || !rightEncoder.valid) return 0.5f;
  float leftExtension = normalizedLegExtension(leftEncoder.angleDeg, leftBentDeg, leftExtendedDeg);
  float rightExtension = normalizedLegExtension(rightEncoder.angleDeg, rightBentDeg, rightExtendedDeg);
  return constrain(0.5f * (leftExtension + rightExtension), 0.0f, 1.0f);
}

float calculateTargetRobotExtension() {
  heightCalibrationValid = validateHeightCalibration();
  if (!heightCalibrationValid) return 0.5f;
  float leftExtension = normalizedLegExtension(leftStandTargetDeg, leftBentDeg, leftExtendedDeg);
  float rightExtension = normalizedLegExtension(rightStandTargetDeg, rightBentDeg, rightExtendedDeg);
  return constrain(0.5f * (leftExtension + rightExtension), 0.0f, 1.0f);
}

float calculateRobotHeightCm(float extension) {
  return lerpFloat(MIN_ROBOT_HEIGHT_CM, MAX_ROBOT_HEIGHT_CM, extension);
}

float calculateExtensionForHeightCm(float heightCm) {
  return constrain(
      (heightCm - MIN_ROBOT_HEIGHT_CM) /
          (MAX_ROBOT_HEIGHT_CM - MIN_ROBOT_HEIGHT_CM),
      0.0f, 1.0f);
}

float legAngleForExtension(float bentDeg, float extendedDeg,
                           float extension) {
  float travelDeg = wrap180(extendedDeg - bentDeg);
  return wrap360(bentDeg + travelDeg * constrain(extension, 0.0f, 1.0f));
}

void initializeHeightTargetFromStandPose() {
  heightCalibrationValid = validateHeightCalibration();
  if (heightCalibrationValid) {
    commandedHeightExtension = calculateTargetRobotExtension();
    requestedHeightExtension = commandedHeightExtension;
  } else {
    commandedHeightExtension = 0.5f;
    requestedHeightExtension = 0.5f;
  }
  dynamicCrouchExtension = 0.0f;
  heightTargetInitialized = true;
}

void updateHeightScheduledWheelParameters(float absoluteErrorDeg) {
  float liveExtension = calculateRobotExtension();
  float targetExtension = calculateTargetRobotExtension();
  currentExtension = fmaxf(targetExtension, liveExtension);
  currentRobotHeightCm = calculateRobotHeightCm(currentExtension);
  float kpScale = heightCalibrationValid
      ? lerpFloat(HEIGHT_KP_SCALE_BENT, HEIGHT_KP_SCALE_EXTENDED, currentExtension) : 1.0f;
  float kdScale = heightCalibrationValid
      ? lerpFloat(HEIGHT_KD_SCALE_BENT, HEIGHT_KD_SCALE_EXTENDED, currentExtension) : 1.0f;
  float outputScale = heightCalibrationValid
      ? lerpFloat(HEIGHT_OUTPUT_SCALE_BENT, HEIGHT_OUTPUT_SCALE_EXTENDED, currentExtension) : 1.0f;
  currentWheelKp = KP * kpScale;
  currentWheelKd = KD * kdScale;
  currentWheelMaxOutputUs = constrain((int)lroundf(MAX_WHEEL_OUTPUT_US * outputScale),
      MIN_SCHEDULED_WHEEL_OUTPUT_US, MAX_WHEEL_OUTPUT_US);
  currentWheelSlewUs = heightCalibrationValid
      ? (int)lroundf(lerpFloat((float)HEIGHT_SLEW_BENT_US_PER_STEP,
          (float)HEIGHT_SLEW_EXTENDED_US_PER_STEP, currentExtension))
      : BALANCE_WHEEL_SLEW_US_PER_STEP;
  currentFullPowerErrorDeg = currentWheelMaxOutputUs / fmaxf(currentWheelKp, 1.0f);
  currentFastTiltErrorDeg = 0.0f;
  currentFastOutputRatio = 0.0f;
  currentWheelAuthorityPercent = 100.0f * currentWheelMaxOutputUs / MAX_WHEEL_OUTPUT_US;
}

void neutralWheels() {
  leftWheelPulse = LEFT_WHEEL_NEUTRAL_US;
  rightWheelPulse = RIGHT_WHEEL_NEUTRAL_US;
  leftWheel.writeMicroseconds(leftWheelPulse);
  rightWheel.writeMicroseconds(rightWheelPulse);
}

void resetLegController() {
  leftLegFilteredErrorDeg = 0.0f;
  rightLegFilteredErrorDeg = 0.0f;
  leftLegIntegral = 0.0f;
  rightLegIntegral = 0.0f;
  leftLegPredictedErrorDeg = 0.0f;
  rightLegPredictedErrorDeg = 0.0f;
  leftLegDriveState = LegDriveState::NEUTRAL;
  rightLegDriveState = LegDriveState::NEUTRAL;
  dynamicCrouchExtension = 0.0f;
}

void neutralLegs() {
  leftLegPulse = LEFT_LEG_NEUTRAL_US;
  rightLegPulse = RIGHT_LEG_NEUTRAL_US;
  leftLeg.writeMicroseconds(leftLegPulse);
  rightLeg.writeMicroseconds(rightLegPulse);
  resetLegController();
}

const char *settingsAckName(SettingsAck ack) {
  switch (ack) {
    case SettingsAck::APPLIED: return "applied";
    case SettingsAck::INVALID: return "invalid";
    case SettingsAck::BUSY: return "balance-must-be-off";
    case SettingsAck::STORAGE_ERROR: return "storage-error";
    case SettingsAck::NONE:
    default: return "none";
  }
}

void stopAllActuatorsImmediately(BalanceStopReason reason,
                                 bool clearDashboardArm) {
  lastBalanceStopReason = reason;
  lastBalanceStopAngleDeg = wrap180(angleDeg - (BALANCE_ANGLE + BALANCE_TRIM_DEG));
  lastBalanceStopMs = millis();
  armed = false;
  standingEnabled = false;
  benchModeActive = false;
  jointMovePending = false;
  wheelManualTestActive = false;
  if (clearDashboardArm) {
    dashboardArmRequested = false;
    dashboardArmEdgePending = false;
    dashboardHasSeenOff = false;
  }
  dashboardMotion = DashboardMotion::STOP;
  pidIntegral = 0.0f;
  pidOutput = 0.0f;
  neutralWheels();
  neutralLegs();
}

void beginEmergencyStop(BalanceStopReason reason) {
  stopAllActuatorsImmediately(reason, true);
  emergencyStopActiveUntilMs = millis() + 1200;
  if (Serial.availableForWrite() >= 80) {
    Serial.println("EMERGENCY STOP: wheel and leg outputs neutral immediately.");
  }
}

void beginDashboardOff() {
  stopAllActuatorsImmediately(BalanceStopReason::DASHBOARD_OFF, false);
  Serial.println("DASHBOARD OFF: all wheel and leg outputs neutral immediately.");
}

// ------------------------------ MPU6050 -------------------------------------
bool writeMPU(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readMPU(ImuSample &sample) {
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU6050_ADDRESS, (size_t)14, true) != 14) return false;
  sample.ax = (Wire.read() << 8) | Wire.read();
  sample.ay = (Wire.read() << 8) | Wire.read();
  sample.az = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read();  
  sample.gx = (Wire.read() << 8) | Wire.read();
  sample.gy = (Wire.read() << 8) | Wire.read();
  sample.gz = (Wire.read() << 8) | Wire.read();
  return true;
}

bool initializeMPU() {
  delay(100);
  bool ok = true;
  ok &= writeMPU(0x6B, 0x00);  
  ok &= writeMPU(0x1A, 0x03);  
  ok &= writeMPU(0x1B, 0x08);  
  ok &= writeMPU(0x1C, 0x08);  
  ok &= writeMPU(0x19, 0x01);  
  return ok;
}

void calibrateMPU() {
  Serial.println("Keep TALOS still; calibrating all three gyro axes.");
  constexpr int samplesRequired = 700;
  int validSamples = 0;
  double gyroSum[3] = {};
  double gyroSquares[3] = {};
  double pitchSum = 0.0, rollSum = 0.0, gravitySum = 0.0;
  for (int sampleIndex = 0; sampleIndex < samplesRequired; ++sampleIndex) {
    ImuSample sample;
    if (readMPU(sample)) {
      float rates[3] = {sample.gx / 65.5f, sample.gy / 65.5f, sample.gz / 65.5f};
      for (int axis = 0; axis < 3; ++axis) {
        gyroSum[axis] += rates[axis];
        gyroSquares[axis] += rates[axis] * rates[axis];
      }
      pitchSum += atan2f((float)sample.ay, (float)sample.az) * RAD_TO_DEG;
      rollSum += atan2f((float)sample.ax, (float)sample.az) * RAD_TO_DEG;
      float accelX = sample.ax / 8192.0f;
      float accelY = sample.ay / 8192.0f;
      float accelZ = sample.az / 8192.0f;
      gravitySum += sqrtf(accelX * accelX + accelY * accelY + accelZ * accelZ);
      validSamples++;
    }
    delay(3);
  }
  bool stable = validSamples >= samplesRequired * 0.95f;
  if (validSamples > 0) {
    for (int axis = 0; axis < 3; ++axis) {
      double mean = gyroSum[axis] / validSamples;
      double variance = gyroSquares[axis] / validSamples - mean * mean;
      stable = stable && variance < 0.64 && fabs(mean) < 15.0;
    }
    stable = stable && fabs(gravitySum / validSamples - 1.0) < 0.15;
  }
  if (!stable) {
    sensorFault = true;
    Serial.println("IMU calibration refused: motion, acceleration, or missing samples. Stop movement, then imu-reset.");
    return;
  }
  gyroBiasDps = gyroSum[0] / validSamples;
  rollRateBiasDps = gyroSum[1] / validSamples;
  yawRateBiasDps = gyroSum[2] / validSamples;
  angleDeg = pitchSum / validSamples;
  rollDeg = rollSum / validSamples;
  lastImuSampleUs = micros();
  Serial.printf("MPU ready: pitch %.2f; gyro biases %.3f/%.3f/%.3f dps.\n",
                angleDeg, gyroBiasDps, rollRateBiasDps, yawRateBiasDps);
}

// ------------------------------ AS5600 --------------------------------------
bool readAS5600(TwoWire &bus, EncoderState &encoder, float dtSeconds) {
  bus.beginTransmission(AS5600_ADDRESS);
  bus.write(0x0B);
  if (bus.endTransmission(false) != 0 ||
      bus.requestFrom((uint8_t)AS5600_ADDRESS, (size_t)3, true) != 3) {
    encoder.valid = false;
    return false;
  }
  uint8_t status = bus.read();
  uint16_t highByte = bus.read();
  uint16_t lowByte = bus.read();
  uint16_t raw = (highByte << 8) | lowByte;
  raw &= 0x0FFF;
  encoder.magneticStatus = status;
  encoder.lastReadUs = micros();
  encoder.raw = raw;
  encoder.magnetDetected = (status & 0x20) != 0;
  encoder.valid = encoder.magnetDetected && (status & 0x18) == 0;
  float newAngle = raw * (360.0f / 4096.0f);
  if (!encoder.valid) {
    encoder.angleDeg = newAngle;
    return false;
  }
  uint32_t sampledUs = micros();
  float measuredDt = (sampledUs - encoder.lastValidUs) / 1000000.0f;
  if (encoder.initialized && dtSeconds > 0.0f &&
      measuredDt > 0.0001f && measuredDt < 0.1f) {
    float measuredVelocity = wrap180(newAngle - encoder.previousDeg) / measuredDt;
    float weight = measuredDt / (0.025f + measuredDt);
    encoder.velocityDps += weight * (measuredVelocity - encoder.velocityDps);
  } else {
    encoder.velocityDps = 0.0f;
  }
  encoder.raw = raw;
  encoder.previousDeg = newAngle;
  encoder.angleDeg = newAngle;
  encoder.lastValidUs = sampledUs;
  encoder.initialized = true;
  return true;
}

// --------------------------- Servo control ---------------------------------
int shapeWheelCommand(float output, int maxOutputUs) {
  float magnitude = fabsf(output);
  if (!isfinite(magnitude) || magnitude < WHEEL_ENGAGE_THRESHOLD) return 0;
  float boost = WHEEL_MIN_ACTIVE_US * magnitude /
      (magnitude + fmaxf((float)WHEEL_MIN_ACTIVE_US, 1.0f));
  magnitude = constrain(magnitude + boost, 0.0f, (float)maxOutputUs);
  return (int)lroundf(copysignf(magnitude, output));
}

void updateWheelControl(float dtSeconds) {
  if (wheelManualTestActive) {
    if ((int32_t)(millis() - wheelManualUntilMs) < 0) {
      int leftTestCommand = constrain(leftWheelManualCommandUs, -MAX_WHEEL_OUTPUT_US, MAX_WHEEL_OUTPUT_US);
      int rightTestCommand = constrain(rightWheelManualCommandUs, -MAX_WHEEL_OUTPUT_US, MAX_WHEEL_OUTPUT_US);
      leftWheelPulse = moveToward(leftWheelPulse, LEFT_WHEEL_NEUTRAL_US + LEFT_WHEEL_SIGN * leftTestCommand, 100);
      rightWheelPulse = moveToward(rightWheelPulse, RIGHT_WHEEL_NEUTRAL_US + RIGHT_WHEEL_SIGN * rightTestCommand, 100);
      leftWheel.writeMicroseconds(constrain(leftWheelPulse, 1000, 2000));
      rightWheel.writeMicroseconds(constrain(rightWheelPulse, 1000, 2000));
      return;
    }
    wheelManualTestActive = false;
    leftWheelManualCommandUs = 0;
    rightWheelManualCommandUs = 0;
    neutralWheels();
  }

  float speedScale = serialControlActive ? 0.0f :
      constrain((float)dashboardSpeedPercent / 100.0f, 0.0f, 1.0f);
  bool armAuthorized = serialControlActive || dashboardArmRequested;
  float dashboardLeanDeg = 0.0f;
  int dashboardTurnUs = 0;
  if (dashboardMotion == DashboardMotion::FORWARD) {
    dashboardLeanDeg = DASHBOARD_FORWARD_SIGN * MAX_DASHBOARD_LEAN_DEG * speedScale;
  } else if (dashboardMotion == DashboardMotion::BACKWARD) {
    dashboardLeanDeg = -DASHBOARD_FORWARD_SIGN * MAX_DASHBOARD_LEAN_DEG * speedScale;
  } else if (dashboardMotion == DashboardMotion::LEFT) {
    dashboardTurnUs = -DASHBOARD_TURN_SIGN * lroundf(MAX_DASHBOARD_TURN_US * speedScale);
  } else if (dashboardMotion == DashboardMotion::RIGHT) {
    dashboardTurnUs = DASHBOARD_TURN_SIGN * lroundf(MAX_DASHBOARD_TURN_US * speedScale);
  }

  float targetBalanceAngle = BALANCE_ANGLE + BALANCE_TRIM_DEG + dashboardLeanDeg;
  float error = angleDeg - targetBalanceAngle;
  bool overTilt = fabsf(error) > FALL_ANGLE_DEG;
  bool overSpin = fabsf(yawRateDps) > YAW_SPIN_FAULT_DPS;

  if (armed) {
    if (overTilt) {
      beginEmergencyStop(BalanceStopReason::TILT_LIMIT);
    } else if (overSpin) {
      beginEmergencyStop(BalanceStopReason::YAW_LIMIT);
    } else if (sensorFault) {
      beginEmergencyStop(BalanceStopReason::IMU_FAULT);
    } else if (!armAuthorized) {
      beginDashboardOff();
    }
  }

  if (!armed || !armAuthorized || sensorFault ||
      (int32_t)(millis() - startupCompleteMs) < 0 || overTilt || overSpin) {
    pidIntegral = 0.0f;
    pidOutput = 0.0f;
    neutralWheels();
    return;
  }

  predictedAngleDeg = angleDeg + gyroRateDps * BALANCE_PREDICTION_TIME_S;
  float predictedError = wrap180(predictedAngleDeg - targetBalanceAngle);
  updateHeightScheduledWheelParameters(fabsf(predictedError));
  float candidateIntegral = KI > 0.0f
      ? constrain(pidIntegral + error * dtSeconds, -INTEGRAL_LIMIT, INTEGRAL_LIMIT) : 0.0f;
  float candidateOutput = currentWheelKp * predictedError +
      KI * candidateIntegral + currentWheelKd * gyroRateDps;
  if (abs(shapeWheelCommand(candidateOutput, currentWheelMaxOutputUs)) < currentWheelMaxOutputUs ||
      candidateOutput * error < 0.0f) {
    pidIntegral = candidateIntegral;
  }
  float controller = currentWheelKp * predictedError +
      KI * pidIntegral + currentWheelKd * gyroRateDps;
  pidOutput = WHEEL_CONTROL_SIGN * controller;
  if (fabsf(error) < QUIET_ERROR_DEG && fabsf(gyroRateDps) < QUIET_RATE_DPS) pidOutput = 0.0f;
  pidOutput = constrain(pidOutput, -(float)currentWheelMaxOutputUs,
                        (float)currentWheelMaxOutputUs);
  int command = shapeWheelCommand(pidOutput, currentWheelMaxOutputUs);
  int turnHeadroom = max(0, currentWheelMaxOutputUs - abs(command));
  dashboardTurnUs = constrain(dashboardTurnUs, -turnHeadroom, turnHeadroom);
  int wheelStep = max(1, (int)lroundf(currentWheelSlewUs * dtSeconds * 500.0f));
  int leftCommand = constrain(command + dashboardTurnUs,
                              -currentWheelMaxOutputUs, currentWheelMaxOutputUs);
  int rightCommand = constrain(command - dashboardTurnUs,
                               -currentWheelMaxOutputUs, currentWheelMaxOutputUs);
  leftWheelPulse = moveToward(leftWheelPulse,
                              LEFT_WHEEL_NEUTRAL_US + LEFT_WHEEL_SIGN * leftCommand,
                              wheelStep);
  rightWheelPulse = moveToward(rightWheelPulse,
                               RIGHT_WHEEL_NEUTRAL_US + RIGHT_WHEEL_SIGN * rightCommand,
                               wheelStep);
  leftWheel.writeMicroseconds(constrain(leftWheelPulse, 1000, 2000));
  rightWheel.writeMicroseconds(constrain(rightWheelPulse, 1000, 2000));
}

const char *legDriveStateName(LegDriveState state) {
  switch (state) {
    case LegDriveState::EXTEND: return "extend";
    case LegDriveState::RETRACT: return "bend";
    case LegDriveState::NEUTRAL:
    default: return "neutral";
  }
}

void updateHeightTrajectory(float dtSeconds) {
  if (!heightTargetInitialized) initializeHeightTargetFromStandPose();

  heightCalibrationValid = validateHeightCalibration();
  if (heightCalibrationValid) {
    float speedRatio = constrain((float)legMotionSpeedPercent / 100.0f, 0.0f, 1.0f);
    float speedCmPerSecond = speedRatio <= 0.0f
        ? 0.0f
        : lerpFloat(MIN_LEG_MOTION_SPEED_CM_S,
                    MAX_LEG_MOTION_SPEED_CM_S, speedRatio);
    float trackingError = fmaxf(
        fabsf(wrap180(leftStandTargetDeg - leftEncoder.angleDeg)),
        fabsf(wrap180(rightStandTargetDeg - rightEncoder.angleDeg)));
    heightMotionPaused = trackingError > 4.0f || (armed &&
        (fabsf(angleDeg - BALANCE_ANGLE - BALANCE_TRIM_DEG) > 4.0f ||
         fabsf(gyroRateDps) > 25.0f));
    if (heightMotionPaused) speedCmPerSecond = 0.0f;
    float maximumExtensionStep =
        speedCmPerSecond * dtSeconds /
        (MAX_ROBOT_HEIGHT_CM - MIN_ROBOT_HEIGHT_CM);
    commandedHeightExtension = moveTowardFloat(
        commandedHeightExtension, requestedHeightExtension,
        maximumExtensionStep);
    leftStandTargetDeg = legAngleForExtension(
        leftBentDeg, leftExtendedDeg, commandedHeightExtension);
    rightStandTargetDeg = legAngleForExtension(
        rightBentDeg, rightExtendedDeg, commandedHeightExtension);
  }

  float requestedCrouch = 0.0f;
  if (legAssistEnabled && armed && heightCalibrationValid) {
    float pitchError = fabsf(
        predictedAngleDeg - (BALANCE_ANGLE + BALANCE_TRIM_DEG));
    float errorRisk = constrain(
        (pitchError - DYNAMIC_CROUCH_START_ERROR_DEG) /
            (DYNAMIC_CROUCH_FULL_ERROR_DEG -
             DYNAMIC_CROUCH_START_ERROR_DEG),
        0.0f, 1.0f);
    float rateRisk = constrain(
        (fabsf(gyroRateDps) - DYNAMIC_CROUCH_START_RATE_DPS) /
            (DYNAMIC_CROUCH_FULL_RATE_DPS -
             DYNAMIC_CROUCH_START_RATE_DPS),
        0.0f, 1.0f);
    requestedCrouch = MAX_DYNAMIC_CROUCH_EXTENSION *
        fmaxf(errorRisk, rateRisk);
  }

  float crouchStep =
      (requestedCrouch > dynamicCrouchExtension
           ? DYNAMIC_CROUCH_ENGAGE_PER_S
           : DYNAMIC_CROUCH_RELEASE_PER_S) *
      dtSeconds;
  dynamicCrouchExtension = moveTowardFloat(
      dynamicCrouchExtension, requestedCrouch, crouchStep);
}

int legPulseForTarget(const EncoderState &encoder, float targetDeg,
                      int neutralUs, int supportSign, int encoderSign,
                      int holdBiasUs, int maxSupportOffsetUs,
                      float &filteredErrorDeg, float &predictedErrorDeg,
                      LegDriveState &driveState, float &integral,
                      float dtSeconds) {
  float rawError = encoderSign * wrap180(targetDeg - encoder.angleDeg);
  float velocity = encoderSign * encoder.velocityDps;
  float weight = dtSeconds / (0.015f + dtSeconds);
  filteredErrorDeg += weight * (rawError - filteredErrorDeg);
  predictedErrorDeg = rawError - velocity * LEG_STATE_PREDICTION_TIME_S;
  if (fabsf(rawError) <= LEG_POSITION_DEADBAND_DEG && fabsf(velocity) < 2.0f) {
    integral = 0.0f;
    driveState = LegDriveState::NEUTRAL;
    return neutralUs;
  }
  float positionDemand = copysignf(
      fmaxf(fabsf(rawError) - LEG_POSITION_DEADBAND_DEG, 0.0f), rawError);
  float supportRamp = constrain(positionDemand / LEG_FULL_DRIVE_ERROR_DEG, 0.0f, 1.0f);
  float retractRamp = constrain(-positionDemand / LEG_FULL_DRIVE_ERROR_DEG, 0.0f, 1.0f);
  float demand = LEG_KP * positionDemand - LEG_KD * velocity +
      abs(holdBiasUs) * supportRamp - LEG_RETRACT_MIN_ACTIVE_US * retractRamp;
  if (LEG_KI <= 0.0f || rawError * integral < 0.0f) integral = 0.0f;
  float candidateIntegral = LEG_KI > 0.0f
      ? constrain(integral + rawError * dtSeconds, -30.0f, 30.0f) : 0.0f;
  float candidateDemand = demand + LEG_KI * candidateIntegral;
  if ((candidateDemand < maxSupportOffsetUs &&
       candidateDemand > -LEG_MAX_RETRACT_OFFSET_US) ||
      candidateDemand * rawError < 0.0f) integral = candidateIntegral;
  demand = constrain(demand + LEG_KI * integral,
                     -(float)LEG_MAX_RETRACT_OFFSET_US, (float)maxSupportOffsetUs);
  driveState = fabsf(demand) < 1.0f ? LegDriveState::NEUTRAL :
      (demand > 0.0f ? LegDriveState::EXTEND : LegDriveState::RETRACT);
  return constrain(neutralUs + supportSign * (int)lroundf(demand),
                   LEG_MIN_PULSE_US, LEG_MAX_PULSE_US);
}

float boundedLegTarget(float targetDeg, float bentDeg, float extendedDeg) {
  float travel = wrap180(extendedDeg - bentDeg);
  float offset = constrain(wrap180(targetDeg - bentDeg),
                          fminf(0.0f, travel), fmaxf(0.0f, travel));
  return wrap360(bentDeg + offset);
}

float maximumBendDeg(bool leftSide) {
  if (!extendedHeightCaptured) return 0.0f;
  float travel = leftSide
      ? LEFT_LEG_ENCODER_SIGN * wrap180(leftExtendedDeg - leftBentDeg)
      : RIGHT_LEG_ENCODER_SIGN * wrap180(rightExtendedDeg - rightBentDeg);
  return heightCalibrationValid && travel > 0.0f ? fmaxf(0.0f, travel - 1.0f) : 8.0f;
}

float measuredBendDeg(bool leftSide) {
  return leftSide
      ? LEFT_LEG_ENCODER_SIGN * wrap180(leftExtendedDeg - leftEncoder.angleDeg)
      : RIGHT_LEG_ENCODER_SIGN * wrap180(rightExtendedDeg - rightEncoder.angleDeg);
}

bool requestJointBend(float leftBend, float rightBend, float speedDps) {
  if (!benchModeActive || armed || sensorFault || legEncoderFaultActive ||
      !extendedHeightCaptured || !isfinite(leftBend) || !isfinite(rightBend) ||
      !isfinite(speedDps) || speedDps < 1.0f || speedDps > 15.0f ||
      leftBend < 0.0f || leftBend > maximumBendDeg(true) ||
      rightBend < 0.0f || rightBend > maximumBendDeg(false)) {
    strlcpy(jointCommandState, "refused", sizeof(jointCommandState));
    strlcpy(jointCommandMessage, "Enable supported leg-only hold and use the reported angle limits.",
            sizeof(jointCommandMessage));
    return false;
  }
  leftRequestedJointDeg = wrap360(leftExtendedDeg - LEFT_LEG_ENCODER_SIGN * leftBend);
  rightRequestedJointDeg = wrap360(rightExtendedDeg - RIGHT_LEG_ENCODER_SIGN * rightBend);
  jointSpeedDps = speedDps;
  benchMoveStartedMs = millis();
  benchInitialErrorDeg = fmaxf(
      fabsf(wrap180(leftRequestedJointDeg - leftEncoder.angleDeg)),
      fabsf(wrap180(rightRequestedJointDeg - rightEncoder.angleDeg)));
  strlcpy(jointCommandState, "moving", sizeof(jointCommandState));
  snprintf(jointCommandMessage, sizeof(jointCommandMessage),
      "Bend targets %.1f / %.1f deg at %.1f deg/s.", leftBend, rightBend, speedDps);
  return true;
}

void updateLegControl(float legDt) {
  bool leftOk = readAS5600(Wire, leftEncoder, legDt);
  if (!leftOk) {
    delayMicroseconds(150);
    leftOk = readAS5600(Wire, leftEncoder, legDt);
  }

  bool rightOk = readAS5600(rightEncoderBus, rightEncoder, legDt);
  if (!rightOk) {
    delayMicroseconds(150);
    rightOk = readAS5600(rightEncoderBus, rightEncoder, legDt);
  }

  if (!leftOk || !rightOk) {
    encoderErrors++;
    if (legFaultStreak < 65535) legFaultStreak++;
    legEncoderFaultActive = true;
    bool expired = !leftEncoder.initialized || !rightEncoder.initialized ||
        (uint32_t)(micros() - leftEncoder.lastValidUs) >= LEG_ENCODER_STALE_LIMIT_US ||
        (uint32_t)(micros() - rightEncoder.lastValidUs) >= LEG_ENCODER_STALE_LIMIT_US;
    neutralLegs();
    if ((armed || standingEnabled) && expired) {
      beginEmergencyStop(BalanceStopReason::LEG_HOLD_FAULT);
    }
    return;
  }
  legFaultStreak = 0;
  legEncoderFaultActive = false;

  if (!standingEnabled) {
    neutralLegs();
    return;
  }

  if (benchModeActive) {
    if (jointMovePending) {
      jointMovePending = false;
      requestJointBend(pendingLeftBendDeg, pendingRightBendDeg, jointSpeedDps);
    }
    float trackingError = fmaxf(
        fabsf(wrap180(leftStandTargetDeg - leftEncoder.angleDeg)),
        fabsf(wrap180(rightStandTargetDeg - rightEncoder.angleDeg)));
    float maximumStep = trackingError > 2.0f ? 0.0f : jointSpeedDps * legDt;
    leftStandTargetDeg = wrap360(leftStandTargetDeg + constrain(
        wrap180(leftRequestedJointDeg - leftStandTargetDeg), -maximumStep, maximumStep));
    rightStandTargetDeg = wrap360(rightStandTargetDeg + constrain(
        wrap180(rightRequestedJointDeg - rightStandTargetDeg), -maximumStep, maximumStep));
    float remainingError = fmaxf(
        fabsf(wrap180(leftRequestedJointDeg - leftEncoder.angleDeg)),
        fabsf(wrap180(rightRequestedJointDeg - rightEncoder.angleDeg)));
    bool wrongWay = remainingError > benchInitialErrorDeg + 2.0f &&
        (uint32_t)(millis() - benchMoveStartedMs) > 600;
    bool timedOut = (uint32_t)(millis() - benchMoveStartedMs) > 15000 &&
        remainingError > 1.5f;
    if (!strcmp(jointCommandState, "moving") && (wrongWay || timedOut)) {
      strlcpy(jointCommandState, "tracking-fault", sizeof(jointCommandState));
      strlcpy(jointCommandMessage, "Position did not converge. Motors stopped; check direction, neutral and power.",
              sizeof(jointCommandMessage));
      beginEmergencyStop(BalanceStopReason::LEG_HOLD_FAULT);
      return;
    }
    if (!strcmp(jointCommandState, "moving") && remainingError < 0.6f &&
        fabsf(leftEncoder.velocityDps) < 3.0f && fabsf(rightEncoder.velocityDps) < 3.0f) {
      strlcpy(jointCommandState, "holding", sizeof(jointCommandState));
    }
  } else updateHeightTrajectory(legDt);

  float rollAssist = 0.0f;
  if (legAssistEnabled && armed && heightCalibrationValid) {
    rollAssist = constrain(
        (rollDeg - ROLL_BALANCE_ANGLE) *
                ROLL_ASSIST_DEG_PER_BODY_DEG +
            rollRateDps * ROLL_RATE_ASSIST_DEG_PER_DPS,
        -ROLL_ASSIST_LIMIT_DEG, ROLL_ASSIST_LIMIT_DEG);
  }

  float effectiveHeightExtension = constrain(
      commandedHeightExtension - dynamicCrouchExtension, 0.0f, 1.0f);
  float desiredLeftDeg = heightCalibrationValid && !benchModeActive
      ? legAngleForExtension(leftBentDeg, leftExtendedDeg,
                             effectiveHeightExtension)
      : leftStandTargetDeg;
  float desiredRightDeg = heightCalibrationValid && !benchModeActive
      ? legAngleForExtension(rightBentDeg, rightExtendedDeg,
                             effectiveHeightExtension)
      : rightStandTargetDeg;
  desiredLeftDeg = wrap360(desiredLeftDeg + LEFT_LEG_ENCODER_SIGN * LEFT_ROLL_SIGN * rollAssist);
  desiredRightDeg = wrap360(desiredRightDeg + RIGHT_LEG_ENCODER_SIGN * RIGHT_ROLL_SIGN * rollAssist);
  if (heightCalibrationValid) {
    desiredLeftDeg = boundedLegTarget(desiredLeftDeg, leftBentDeg, leftExtendedDeg);
    desiredRightDeg = boundedLegTarget(desiredRightDeg, rightBentDeg, rightExtendedDeg);
  }

  leftActualTargetDeg = desiredLeftDeg;
  rightActualTargetDeg = desiredRightDeg;
  int desiredLeftPulse = legPulseForTarget(
      leftEncoder, desiredLeftDeg, LEFT_LEG_NEUTRAL_US,
      LEFT_LEG_SUPPORT_SIGN, LEFT_LEG_ENCODER_SIGN,
      LEFT_LEG_HOLD_BIAS_US, LEFT_LEG_MAX_SUPPORT_OFFSET_US,
      leftLegFilteredErrorDeg, leftLegPredictedErrorDeg,
      leftLegDriveState, leftLegIntegral, legDt);

  int desiredRightPulse = legPulseForTarget(
      rightEncoder, desiredRightDeg, RIGHT_LEG_NEUTRAL_US,
      RIGHT_LEG_SUPPORT_SIGN, RIGHT_LEG_ENCODER_SIGN,
      RIGHT_LEG_HOLD_BIAS_US, RIGHT_LEG_MAX_SUPPORT_OFFSET_US,
      rightLegFilteredErrorDeg, rightLegPredictedErrorDeg,
      rightLegDriveState, rightLegIntegral, legDt);

  if (benchModeActive) {
    desiredLeftPulse = constrain(desiredLeftPulse, LEFT_LEG_NEUTRAL_US - 120, LEFT_LEG_NEUTRAL_US + 120);
    desiredRightPulse = constrain(desiredRightPulse, RIGHT_LEG_NEUTRAL_US - 120, RIGHT_LEG_NEUTRAL_US + 120);
  }
  bool leftBraking =
      (desiredLeftPulse - LEFT_LEG_NEUTRAL_US) * LEFT_LEG_SUPPORT_SIGN < 0;
  bool rightBraking =
      (desiredRightPulse - RIGHT_LEG_NEUTRAL_US) * RIGHT_LEG_SUPPORT_SIGN < 0;
  int leftStep = desiredLeftPulse == LEFT_LEG_NEUTRAL_US
      ? LEG_NEUTRAL_RETURN_STEP_US
      : (leftBraking
             ? LEG_BRAKE_SLEW_US_PER_STEP
             : (fabsf(leftLegPredictedErrorDeg) >= LEG_FULL_DRIVE_ERROR_DEG
                    ? LEG_RECOVERY_SLEW_US_PER_STEP
                    : LEG_SLEW_US_PER_STEP));
  int rightStep = desiredRightPulse == RIGHT_LEG_NEUTRAL_US
      ? LEG_NEUTRAL_RETURN_STEP_US
      : (rightBraking
             ? LEG_BRAKE_SLEW_US_PER_STEP
             : (fabsf(rightLegPredictedErrorDeg) >= LEG_FULL_DRIVE_ERROR_DEG
                    ? LEG_RECOVERY_SLEW_US_PER_STEP
                    : LEG_SLEW_US_PER_STEP));

  leftStep = max(1, (int)lroundf(leftStep * legDt * 100.0f));
  rightStep = max(1, (int)lroundf(rightStep * legDt * 100.0f));
  leftLegPulse = moveToward(leftLegPulse, desiredLeftPulse, leftStep);
  rightLegPulse = moveToward(rightLegPulse, desiredRightPulse, rightStep);

  leftLeg.writeMicroseconds(leftLegPulse);
  rightLeg.writeMicroseconds(rightLegPulse);
}

// -------------------------- Saved calibration -------------------------------
void loadCalibration() {
  nvs_handle_t handle;
  if (nvs_open("talos", NVS_READONLY, &handle) != ESP_OK) return;
  StoredCalibration stored = {};
  size_t length = sizeof(stored);
  int32_t storedWheelControlSign = 1;
  if (nvs_get_i32(handle, "wsign_fixed", &storedWheelControlSign) == ESP_OK) {
    WHEEL_CONTROL_SIGN = storedWheelControlSign < 0 ? -1 : 1;
  }
  int32_t storedLeftFixedBias = LEFT_LEG_HOLD_BIAS_US;
  int32_t storedRightFixedBias = RIGHT_LEG_HOLD_BIAS_US;
  if (nvs_get_i32(handle, "lguard_v5", &storedLeftFixedBias) == ESP_OK &&
      storedLeftFixedBias >= -180 && storedLeftFixedBias <= -20) {
    LEFT_LEG_HOLD_BIAS_US = storedLeftFixedBias;
  }
  if (nvs_get_i32(handle, "rguard_v5", &storedRightFixedBias) == ESP_OK &&
      storedRightFixedBias >= 50 && storedRightFixedBias <= 320) {
    RIGHT_LEG_HOLD_BIAS_US = storedRightFixedBias;
  }
  if (nvs_get_blob(handle, "cal", &stored, &length) == ESP_OK &&
      stored.magic == CALIBRATION_MAGIC && length == sizeof(stored)) {
    leftStandTargetDeg = stored.leftStand;
    rightStandTargetDeg = stored.rightStand;
    BALANCE_ANGLE = stored.balance;
    ROLL_BALANCE_ANGLE = stored.rollBalance;
    standPoseCaptured = true;
    leftBentDeg = stored.leftBentDeg;
    rightBentDeg = stored.rightBentDeg;
    leftExtendedDeg = stored.leftExtendedDeg;
    rightExtendedDeg = stored.rightExtendedDeg;
    bentHeightCaptured = stored.bentCaptured != 0;
    extendedHeightCaptured = stored.extendedCaptured != 0;
    heightCalibrationValid = validateHeightCalibration();
  }
  nvs_close(handle);
}

void saveCalibration() {
  nvs_handle_t handle;
  if (nvs_open("talos", NVS_READWRITE, &handle) == ESP_OK) {
    StoredCalibration stored = {
      CALIBRATION_MAGIC,
      leftStandTargetDeg, rightStandTargetDeg,
      BALANCE_ANGLE, ROLL_BALANCE_ANGLE,
      0, 0,  // unused old sign fields
      LEFT_LEG_HOLD_BIAS_US, RIGHT_LEG_HOLD_BIAS_US,
      leftBentDeg, rightBentDeg,
      leftExtendedDeg, rightExtendedDeg,
      bentHeightCaptured ? 1 : 0,
      extendedHeightCaptured ? 1 : 0
    };
    nvs_set_blob(handle, "cal", &stored, sizeof(stored));
    nvs_set_i32(handle, "wsign_fixed", WHEEL_CONTROL_SIGN);
    nvs_set_i32(handle, "lguard_v5", LEFT_LEG_HOLD_BIAS_US);
    nvs_set_i32(handle, "rguard_v5", RIGHT_LEG_HOLD_BIAS_US);
    nvs_commit(handle);
    nvs_close(handle);
    Serial.println("Calibration saved!");
  }
}

bool isAllowedFallAngle(int value) {
  return value == 30 || value == 60 || value == 80;
}

bool runtimeSettingsValid(const StoredRuntimeSettings &settings) {
  return settings.magic == RUNTIME_SETTINGS_MAGIC &&
         settings.version == RUNTIME_SETTINGS_VERSION &&
         settings.size == sizeof(StoredRuntimeSettings) &&
         isfinite(settings.kp) && settings.kp >= MIN_KP && settings.kp <= MAX_KP &&
         isfinite(settings.ki) && settings.ki >= MIN_KI && settings.ki <= MAX_KI &&
         isfinite(settings.kd) && settings.kd >= MIN_KD && settings.kd <= MAX_KD &&
         isfinite(settings.balanceTrimDeg) &&
         settings.balanceTrimDeg >= MIN_BALANCE_TRIM_DEG &&
         settings.balanceTrimDeg <= MAX_BALANCE_TRIM_DEG &&
         isfinite(settings.predictionTimeS) &&
         settings.predictionTimeS >= MIN_PREDICTION_TIME_S &&
         settings.predictionTimeS <= MAX_PREDICTION_TIME_S &&
         settings.maximumWheelOutputUs >= MIN_MAX_WHEEL_OUTPUT_US &&
         settings.maximumWheelOutputUs <= MAX_MAX_WHEEL_OUTPUT_US &&
         settings.minimumActiveWheelOutputUs >= MIN_ACTIVE_WHEEL_OUTPUT_US &&
         settings.minimumActiveWheelOutputUs <= MAX_ACTIVE_WHEEL_OUTPUT_US &&
         settings.minimumActiveWheelOutputUs < settings.maximumWheelOutputUs &&
         isAllowedFallAngle(settings.fallAngleDeg);
}

StoredRuntimeSettings currentRuntimeSettings() {
  return {
    RUNTIME_SETTINGS_MAGIC,
    RUNTIME_SETTINGS_VERSION,
    (uint16_t)sizeof(StoredRuntimeSettings),
    KP,
    KI,
    KD,
    BALANCE_TRIM_DEG,
    BALANCE_PREDICTION_TIME_S,
    MAX_WHEEL_OUTPUT_US,
    WHEEL_MIN_ACTIVE_US,
    (int32_t)lroundf(FALL_ANGLE_DEG),
    0,
    0
  };
}

void applyRuntimeSettings(const StoredRuntimeSettings &settings) {
  KP = settings.kp;
  KI = settings.ki;
  KD = settings.kd;
  BALANCE_TRIM_DEG = settings.balanceTrimDeg;
  BALANCE_PREDICTION_TIME_S = settings.predictionTimeS;
  MAX_WHEEL_OUTPUT_US = settings.maximumWheelOutputUs;
  WHEEL_MIN_ACTIVE_US = settings.minimumActiveWheelOutputUs;
  FALL_ANGLE_DEG = (float)settings.fallAngleDeg;
}

bool saveRuntimeSettings() {
  StoredRuntimeSettings settings = currentRuntimeSettings();
  if (!runtimeSettingsValid(settings)) return false;

  nvs_handle_t handle;
  if (nvs_open("talos_cfg", NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t result = nvs_set_blob(handle, "runtime_v1", &settings, sizeof(settings));
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}

void loadRuntimeSettings() {
  nvs_handle_t handle;
  if (nvs_open("talos_cfg", NVS_READONLY, &handle) != ESP_OK) return;

  StoredRuntimeSettings settings = {};
  size_t length = sizeof(settings);
  esp_err_t result = nvs_get_blob(handle, "runtime_v1", &settings, &length);
  nvs_close(handle);
  if (result == ESP_OK && length == sizeof(settings) &&
      runtimeSettingsValid(settings)) {
    applyRuntimeSettings(settings);
    Serial.printf(
        "Runtime settings restored: cutoff %.0f deg, KP %.1f, KD %.2f, prediction %.0f ms.\n",
        FALL_ANGLE_DEG, KP, KD, BALANCE_PREDICTION_TIME_S * 1000.0f);
  }
}

bool saveLastWifiCommandId(const char *commandId) {
  if (!commandId || commandId[0] == '\0') return false;
  nvs_handle_t handle;
  if (nvs_open("talos_cfg", NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t result = nvs_set_str(handle, "wifi_cmd_v1", commandId);
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}

void loadLastWifiCommandId() {
  nvs_handle_t handle;
  if (nvs_open("talos_cfg", NVS_READONLY, &handle) != ESP_OK) return;
  size_t length = sizeof(lastProcessedWifiCommandId);
  const esp_err_t result = nvs_get_str(
      handle, "wifi_cmd_v1", lastProcessedWifiCommandId, &length);
  nvs_close(handle);
  if (result != ESP_OK || length == 0 ||
      length > sizeof(lastProcessedWifiCommandId)) {
    lastProcessedWifiCommandId[0] = '\0';
  }
}

bool saveLastPostureCommandId(const char *commandId) {
  if (!commandId || commandId[0] == '\0') return false;
  nvs_handle_t handle;
  if (nvs_open("talos_cfg", NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t result = nvs_set_str(handle, "pose_cmd_v1", commandId);
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}

void loadLastPostureCommandId() {
  nvs_handle_t handle;
  if (nvs_open("talos_cfg", NVS_READONLY, &handle) != ESP_OK) return;
  size_t length = sizeof(lastProcessedPostureCommandId);
  const esp_err_t result = nvs_get_str(
      handle, "pose_cmd_v1", lastProcessedPostureCommandId, &length);
  nvs_close(handle);
  if (result != ESP_OK || length == 0 ||
      length > sizeof(lastProcessedPostureCommandId)) {
    lastProcessedPostureCommandId[0] = '\0';
  }
}

// ---------------------- Desired-height pose capture -------------------------
bool sampleEncoderPose(float &leftDegOut, float &rightDegOut) {
  int validSamples = 0;
  float leftSinSum = 0.0f, leftCosSum = 0.0f, rightSinSum = 0.0f, rightCosSum = 0.0f;
  for (int attempt = 0; attempt < 100 && validSamples < 60; ++attempt) {
    if (readAS5600(Wire, leftEncoder, 0.0f) && readAS5600(rightEncoderBus, rightEncoder, 0.0f)) {
      leftSinSum += sinf(leftEncoder.angleDeg * DEG_TO_RAD);
      leftCosSum += cosf(leftEncoder.angleDeg * DEG_TO_RAD);
      rightSinSum += sinf(rightEncoder.angleDeg * DEG_TO_RAD);
      rightCosSum += cosf(rightEncoder.angleDeg * DEG_TO_RAD);
      validSamples++;
    }
    delay(10);
  }
  if (validSamples < 60) return false;
  leftDegOut = atan2f(leftSinSum, leftCosSum) * RAD_TO_DEG;
  rightDegOut = atan2f(rightSinSum, rightCosSum) * RAD_TO_DEG;
  if (leftDegOut < 0.0f) leftDegOut += 360.0f;
  if (rightDegOut < 0.0f) rightDegOut += 360.0f;
  return true;
}

bool captureHeightEndpoint(bool captureBent) {
  armed = false;
  standingEnabled = false;
  neutralWheels();
  neutralLegs();
  delay(150);
  float sampledLeft = 0.0f, sampledRight = 0.0f;
  if (!sampleEncoderPose(sampledLeft, sampledRight)) return false;
  if (captureBent) {
    leftBentDeg = sampledLeft;
    rightBentDeg = sampledRight;
    bentHeightCaptured = true;
    Serial.printf("BENT HEIGHT CAPTURED (16.2 cm): L=%.2f R=%.2f\n", leftBentDeg, rightBentDeg);
  } else {
    leftExtendedDeg = sampledLeft;
    rightExtendedDeg = sampledRight;
    extendedHeightCaptured = true;
    Serial.printf("EXTENDED HEIGHT CAPTURED (29.2 cm): L=%.2f R=%.2f\n", leftExtendedDeg, rightExtendedDeg);
  }
  heightCalibrationValid = validateHeightCalibration();
  saveCalibration();
  if (bentHeightCaptured && extendedHeightCaptured && !heightCalibrationValid) {
    Serial.println("WARNING: bent and extended encoder angles are too similar. Capture again.");
  }
  return true;
}

bool captureDesiredHeightPose() {
  armed = false;
  standingEnabled = false;
  neutralWheels();
  neutralLegs();
  delay(150);
  constexpr int requiredSamples = 30;
  int validSamples = 0;
  float leftSinSum = 0.0f, leftCosSum = 0.0f, rightSinSum = 0.0f, rightCosSum = 0.0f, pitchSum = 0.0f, rollSum = 0.0f;
  for (int attempt = 0; attempt < 240 && validSamples < requiredSamples; ++attempt) {
    ImuSample sample;
    if (readMPU(sample) && readAS5600(Wire, leftEncoder, 0.0f) && readAS5600(rightEncoderBus, rightEncoder, 0.0f)) {
      leftSinSum += sinf(leftEncoder.angleDeg * DEG_TO_RAD); leftCosSum += cosf(leftEncoder.angleDeg * DEG_TO_RAD);
      rightSinSum += sinf(rightEncoder.angleDeg * DEG_TO_RAD); rightCosSum += cosf(rightEncoder.angleDeg * DEG_TO_RAD);
      pitchSum += atan2f((float)sample.ay, (float)sample.az) * RAD_TO_DEG;
      rollSum += atan2f((float)sample.ax, (float)sample.az) * RAD_TO_DEG;
      validSamples++;
    }
    delay(5);
  }
  if (validSamples < requiredSamples) return false;
  leftStandTargetDeg = atan2f(leftSinSum, leftCosSum) * RAD_TO_DEG;
  rightStandTargetDeg = atan2f(rightSinSum, rightCosSum) * RAD_TO_DEG;
  if (leftStandTargetDeg < 0.0f) leftStandTargetDeg += 360.0f;
  if (rightStandTargetDeg < 0.0f) rightStandTargetDeg += 360.0f;
  angleDeg = pitchSum / validSamples;
  rollDeg = rollSum / validSamples;
  resetLegController();
  neutralLegs();
  standPoseCaptured = true;
  standingEnabled = false;
  initializeHeightTargetFromStandPose();
  saveCalibration();
  Serial.printf("STAND POSE CAPTURED: L=%.2f deg, R=%.2f deg. Legs remain OFF.\n", leftStandTargetDeg, rightStandTargetDeg);
  Serial.println("Leg pose saved; balance zero unchanged. Send hold, then arm-saved when upright.");
  return true;
}

bool captureBalancePitchOnly() {
  if (armed || standingEnabled || wheelManualTestActive || sensorFault ||
      fabsf(angleDeg) > 20.0f || fabsf(gyroRateDps) > 2.0f) {
    Serial.println("Zero refused: support upright and still, with all motors off.");
    return false;
  }
  constexpr int requiredSamples = 120;
  int validSamples = 0;
  double sinSum = 0.0, cosSum = 0.0;
  for (int attempt = 0; attempt < 180 && validSamples < requiredSamples; ++attempt) {
    ImuSample sample;
    if (readMPU(sample)) {
      float pitch = atan2f((float)sample.ay, (float)sample.az) * RAD_TO_DEG;
      float rate = sample.gx / 65.5f - gyroBiasDps;
      if (fabsf(pitch) > 20.0f || fabsf(rate) > 2.0f) return false;
      sinSum += sinf(pitch * DEG_TO_RAD);
      cosSum += cosf(pitch * DEG_TO_RAD);
      validSamples++;
    }
    delay(3);
  }
  if (validSamples < requiredSamples) return false;
  BALANCE_ANGLE = atan2f((float)sinSum, (float)cosSum) * RAD_TO_DEG;
  angleDeg = BALANCE_ANGLE;
  pidIntegral = 0.0f;
  pidOutput = 0.0f;
  saveCalibration();
  Serial.printf("Wheel balance zero captured: %.2f deg\n", BALANCE_ANGLE);
  return true;
}

void enableStandingHold() {
  resetLegController();
  initializeHeightTargetFromStandPose();
  leftLegPulse = LEFT_LEG_NEUTRAL_US;
  rightLegPulse = RIGHT_LEG_NEUTRAL_US;
  leftLeg.writeMicroseconds(leftLegPulse);
  rightLeg.writeMicroseconds(rightLegPulse);
  standingEnabled = true;
  Serial.printf("GUARDED LEG HOLD ON: target L=%.2f R=%.2f support=%d/%d.\n",
                leftStandTargetDeg, rightStandTargetDeg,
                LEFT_LEG_HOLD_BIAS_US, RIGHT_LEG_HOLD_BIAS_US);
}

void servicePendingPostureCommand() {
  PostureCommandAction action = pendingPostureCommand;
  if (action == PostureCommandAction::NONE) return;

  pendingPostureCommand = PostureCommandAction::NONE;
  strlcpy(postureCommandAckId, pendingPostureCommandId,
          sizeof(postureCommandAckId));
  strlcpy(postureCommandState, "capturing",
          sizeof(postureCommandState));
  strlcpy(postureCommandMessage,
          "Sampling both leg encoders. Keep TALOS supported and still.",
          sizeof(postureCommandMessage));

  bool success = false;
  if (action == PostureCommandAction::CAPTURE_BENT) {
    success = captureHeightEndpoint(true);
  } else if (action == PostureCommandAction::CAPTURE_EXTENDED) {
    success = captureHeightEndpoint(false);
  } else if (action == PostureCommandAction::BENCH_HOLD) {
    success = !sensorFault && leftEncoder.valid && rightEncoder.valid && !armed;
    if (success) {
      leftStandTargetDeg = leftEncoder.angleDeg;
      rightStandTargetDeg = rightEncoder.angleDeg;
      standPoseCaptured = true;
      enableStandingHold();
      benchModeActive = true;
      leftRequestedJointDeg = leftStandTargetDeg;
      rightRequestedJointDeg = rightStandTargetDeg;
      localOffBaselineRevision = dashboardMotionRevision;
      localOffBaselineReady = true;
      strlcpy(jointCommandState, "holding", sizeof(jointCommandState));
      strlcpy(jointCommandMessage, "Supported leg-only hold enabled. Wheels remain neutral.",
              sizeof(jointCommandMessage));
    }
  }

  if (success) {
    strlcpy(postureCommandState, "captured",
            sizeof(postureCommandState));
    strlcpy(
        postureCommandMessage,
        action == PostureCommandAction::BENCH_HOLD
            ? "Supported leg-only hold enabled; wheels neutral."
            : action == PostureCommandAction::CAPTURE_BENT
                ? "Bent endpoint captured at 16.2 cm."
                : "Extended endpoint captured at 29.2 cm.",
        sizeof(postureCommandMessage));
  } else {
    strlcpy(postureCommandState, "sensor-error",
            sizeof(postureCommandState));
    strlcpy(postureCommandMessage,
            "Capture failed. Check both AS5600 magnets and I2C wiring.",
            sizeof(postureCommandMessage));
  }
  pendingPostureCommandId[0] = '\0';
}

bool automaticInitializeAndCalibrateMPU() {
  armed = false;
  standingEnabled = false;
  wheelManualTestActive = false;
  neutralWheels();
  neutralLegs();

  sensorFault = false;
  imuFaultStreak = 0;
  imuErrors = 0;

  // Some MPU6050 modules need extra time after battery power is applied.
  delay(250);
  if (!initializeMPU()) {
    sensorFault = true;
    Serial.println("AUTO: MPU6050 did not acknowledge; retrying.");
    return false;
  }

  calibrateMPU();
  if (sensorFault) {
    Serial.println("AUTO: MPU6050 calibration failed; retrying.");
    return false;
  }

  imuFaultStreak = 0;
  Serial.println("AUTO: MPU6050 initialized and calibrated.");
  return true;
}

bool automaticStartupStable() {
  if (sensorFault || legEncoderFaultActive || !leftEncoder.valid || !rightEncoder.valid) return false;
  if (fabsf(wrap180(angleDeg - BALANCE_ANGLE - BALANCE_TRIM_DEG)) >
      ARM_ANGLE_LIMIT_DEG || fabsf(angleDeg) > 20.0f ||
      fabsf(rollDeg - ROLL_BALANCE_ANGLE) > 12.0f) return false;
  if (fabsf(gyroRateDps) > AUTO_MAX_GYRO_RATE_DPS) return false;
  if (fabsf(rollRateDps) > AUTO_MAX_GYRO_RATE_DPS) return false;
  if (fabsf(yawRateDps) > AUTO_MAX_YAW_RATE_DPS) return false;
  if (fabsf(accelMagG - 1.0f) > AUTO_MAX_ACCEL_ERROR_G) return false;
  return true;
}

bool automaticStableFor(uint32_t requiredMs) {
  if (!automaticStartupStable()) {
    autoStableSinceMs = 0;
    return false;
  }
  if (autoStableSinceMs == 0) autoStableSinceMs = millis();
  return (uint32_t)(millis() - autoStableSinceMs) >= requiredMs;
}


void enterAutoState(AutoStartupState state) {
  autoStartupState = state;
  autoStateSinceMs = millis();
  autoStableSinceMs = 0;
}

const char *autoStateName(AutoStartupState state) {
  switch (state) {
    case AutoStartupState::WAIT_INITIAL_DELAY: return "sensor-startup";
    case AutoStartupState::CAPTURE_STAND_POSE: return "capturing-stand";
    case AutoStartupState::ENABLE_LEG_HOLD: return "enabling-leg-hold";
    case AutoStartupState::WAIT_LEG_SETTLE: return "legs-settling";
    case AutoStartupState::CAPTURE_BALANCE_ZERO: return "capturing-balance";
    case AutoStartupState::WAIT_BEFORE_ARM: return "ready";
    case AutoStartupState::RUNNING: return "running";
    case AutoStartupState::WAIT_REARM: return "waiting-for-arm";
    default: return "fault";
  }
}

const char *dashboardMotionName(DashboardMotion motion) {
  switch (motion) {
    case DashboardMotion::FORWARD: return "forward";
    case DashboardMotion::BACKWARD: return "backward";
    case DashboardMotion::LEFT: return "left";
    case DashboardMotion::RIGHT: return "right";
    default: return "stop";
  }
}

const char *balanceStopReasonName(BalanceStopReason reason) {
  switch (reason) {
    case BalanceStopReason::DASHBOARD_OFF: return "dashboard-off";
    case BalanceStopReason::TILT_LIMIT: return "tilt-limit";
    case BalanceStopReason::YAW_LIMIT: return "yaw-limit";
    case BalanceStopReason::IMU_FAULT: return "imu-fault";
    case BalanceStopReason::CONTROL_TIMING: return "control-timing";
    case BalanceStopReason::LEG_HOLD_FAULT: return "leg-hold-fault";
    case BalanceStopReason::MANUAL_STOP: return "manual-stop";
    case BalanceStopReason::NONE:
    default: return "none";
  }
}

DashboardMotion parseDashboardMotion(const char *value) {
  if (!value) return DashboardMotion::STOP;
  if (!strcmp(value, "forward")) return DashboardMotion::FORWARD;
  if (!strcmp(value, "backward")) return DashboardMotion::BACKWARD;
  if (!strcmp(value, "left")) return DashboardMotion::LEFT;
  if (!strcmp(value, "right")) return DashboardMotion::RIGHT;
  return DashboardMotion::STOP;
}

String firebaseUrl(const char *path) {
  return String(FIREBASE_DATABASE_URL) + "/" + path + ".json";
}

int firebaseGet(const char *path, String &response) {
  if (!talosWifiManager.isConnected()) return -1;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(1800);
  http.setTimeout(2200);
  if (!http.begin(client, firebaseUrl(path))) return -2;
  int code = http.GET();
  response = http.getString();
  http.end();
  return code;
}

int firebasePatch(const char *path, const String &body) {
  if (!talosWifiManager.isConnected()) return -1;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(1800);
  http.setTimeout(2200);
  if (!http.begin(client, firebaseUrl(path))) return -2;
  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest("PATCH", body);
  http.end();
  return code;
}

bool runtimeSettingsEqual(const StoredRuntimeSettings &a,
                          const StoredRuntimeSettings &b) {
  return fabsf(a.kp - b.kp) < 0.001f &&
         fabsf(a.ki - b.ki) < 0.001f &&
         fabsf(a.kd - b.kd) < 0.001f &&
         fabsf(a.balanceTrimDeg - b.balanceTrimDeg) < 0.001f &&
         fabsf(a.predictionTimeS - b.predictionTimeS) < 0.0001f &&
         a.maximumWheelOutputUs == b.maximumWheelOutputUs &&
         a.minimumActiveWheelOutputUs == b.minimumActiveWheelOutputUs &&
         a.fallAngleDeg == b.fallAngleDeg;
}

void processRuntimeSettings(JsonDocument &control) {
  uint64_t revision = control["settingsRevision"] | 0ULL;
  if (revision == 0 || revision == lastProcessedSettingsRevision) return;

  dashboardSettingsRevision = revision;
  settingsAckRevision = revision;

  if (armed || standingEnabled) {
    settingsAck = SettingsAck::BUSY;
    return;
  }
  // A request received while active remains pending in Firebase. Mark it
  // processed only after the robot is safe so it is automatically retried
  // after every actuator output is off instead of being silently lost.
  lastProcessedSettingsRevision = revision;

  StoredRuntimeSettings previous = currentRuntimeSettings();
  StoredRuntimeSettings candidate = previous;
  JsonObjectConst tuning = control["tuning"].as<JsonObjectConst>();

  candidate.fallAngleDeg =
      control["fallAngleLimitDeg"] | candidate.fallAngleDeg;
  candidate.kp = tuning["kp"] | candidate.kp;
  candidate.ki = tuning["ki"] | candidate.ki;
  candidate.kd = tuning["kd"] | candidate.kd;
  candidate.balanceTrimDeg =
      tuning["balanceTrimDeg"] | candidate.balanceTrimDeg;
  float predictionTimeMs =
      tuning["predictionTimeMs"] | (candidate.predictionTimeS * 1000.0f);
  candidate.predictionTimeS = predictionTimeMs / 1000.0f;
  candidate.maximumWheelOutputUs =
      tuning["maxWheelOutputUs"] | candidate.maximumWheelOutputUs;
  candidate.minimumActiveWheelOutputUs =
      tuning["minActiveWheelOutputUs"] |
      candidate.minimumActiveWheelOutputUs;
  if (!runtimeSettingsValid(candidate)) {
    settingsAck = SettingsAck::INVALID;
    return;
  }

  if (!runtimeSettingsEqual(previous, candidate)) {
    applyRuntimeSettings(candidate);
    if (!saveRuntimeSettings()) {
      applyRuntimeSettings(previous);
      settingsAck = SettingsAck::STORAGE_ERROR;
      return;
    }
  }

  settingsAck = SettingsAck::APPLIED;
  Serial.printf(
      "Dashboard settings applied: cutoff %.0f deg, KP %.1f, KD %.2f, prediction %.0f ms.\n",
      FALL_ANGLE_DEG, KP, KD, BALANCE_PREDICTION_TIME_S * 1000.0f);
}

void processHeightCommand(JsonDocument &control) {
  const uint64_t revision = control["heightCommandRevision"] | 0ULL;
  dashboardHeightCommandRevision = revision;

  // A retained Firebase value must never move the legs immediately after a
  // reboot. The first valid cloud snapshot establishes the replay baseline;
  // the operator must press Apply height again for a fresh revision.
  if (!heightCommandReplayGuardReady) {
    heightCommandReplayGuardReady = true;
    lastProcessedHeightCommandRevision = revision;
    if (revision != 0) {
      heightCommandAckRevision = revision;
      strlcpy(heightCommandState, "startup-guard",
              sizeof(heightCommandState));
      strlcpy(heightCommandMessage,
              "Retained height request ignored after startup; apply again.",
              sizeof(heightCommandMessage));
    }
    return;
  }

  if (revision == 0 || revision == lastProcessedHeightCommandRevision) return;
  lastProcessedHeightCommandRevision = revision;
  heightCommandAckRevision = revision;

  int requestedPercent = control["legPosition"] | -1;
  int requestedSpeed = control["legSpeed"] | -1;
  if (requestedPercent < 0 || requestedPercent > 100 ||
      requestedSpeed < 0 || requestedSpeed > 100) {
    strlcpy(heightCommandState, "invalid", sizeof(heightCommandState));
    strlcpy(heightCommandMessage,
            "Height and movement speed must be between 0 and 100 percent.",
            sizeof(heightCommandMessage));
    return;
  }

  heightCalibrationValid = validateHeightCalibration();
  if (!heightCalibrationValid) {
    strlcpy(heightCommandState, "calibration-required",
            sizeof(heightCommandState));
    strlcpy(heightCommandMessage,
            "Capture the 16.2 cm and 29.2 cm endpoints first.",
            sizeof(heightCommandMessage));
    return;
  }
  if (!standingEnabled || sensorFault || legEncoderFaultActive) {
    strlcpy(heightCommandState, "leg-hold-required",
            sizeof(heightCommandState));
    strlcpy(heightCommandMessage,
            "Height changes require active leg hold and healthy sensors.",
            sizeof(heightCommandMessage));
    return;
  }

  requestedHeightExtension = (float)requestedPercent / 100.0f;
  legMotionSpeedPercent = requestedSpeed;
  strlcpy(heightCommandState, "accepted", sizeof(heightCommandState));
  snprintf(heightCommandMessage, sizeof(heightCommandMessage),
           "Moving toward %.1f cm at %d%% leg speed.",
           calculateRobotHeightCm(requestedHeightExtension),
           legMotionSpeedPercent);
}

void processJointCommand(JsonDocument &control) {
  uint64_t revision = control["jointCommandRevision"] | (uint64_t)0;
  if (!jointReplayGuardReady) {
    jointReplayGuardReady = true;
    lastJointCommandRevision = revision;
    return;
  }
  if (revision == 0 || revision == lastJointCommandRevision) return;
  lastJointCommandRevision = revision;
  jointCommandAckRevision = revision;
  JsonObjectConst request = control["jointTargets"].as<JsonObjectConst>();
  float leftBend = request["leftBendDeg"] | NAN;
  float rightBend = request["rightBendDeg"] | NAN;
  float speed = request["speedDps"] | 6.0f;
  if (serialControlActive || !benchModeActive || armed || !extendedHeightCaptured ||
      !isfinite(leftBend) || !isfinite(rightBend) || !isfinite(speed) ||
      speed < 1.0f || speed > 15.0f || leftBend < 0.0f || rightBend < 0.0f ||
      leftBend > maximumBendDeg(true) || rightBend > maximumBendDeg(false)) {
    strlcpy(jointCommandState, "refused", sizeof(jointCommandState));
    strlcpy(jointCommandMessage, "Enable supported leg-only hold and use the reported angle limits.",
            sizeof(jointCommandMessage));
    return;
  }
  pendingLeftBendDeg = leftBend;
  pendingRightBendDeg = rightBend;
  jointSpeedDps = speed;
  jointMovePending = true;
}

void processPostureCommand(JsonDocument &control) {
  JsonObjectConst command = control["postureCommand"].as<JsonObjectConst>();
  if (!postureCommandReplayGuardReady) {
    postureCommandReplayGuardReady = true;
    if (!command.isNull()) {
      const char *retainedId = command["id"] | "";
      if (retainedId[0] != '\0' &&
          strcmp(retainedId, lastProcessedPostureCommandId) != 0) {
        strlcpy(lastProcessedPostureCommandId, retainedId,
                sizeof(lastProcessedPostureCommandId));
        strlcpy(postureCommandAckId, retainedId,
                sizeof(postureCommandAckId));
        strlcpy(postureCommandState, "startup-guard",
                sizeof(postureCommandState));
        strlcpy(postureCommandMessage,
                "Retained calibration request ignored after startup; request again.",
                sizeof(postureCommandMessage));
      }
    }
    return;
  }
  if (command.isNull()) return;

  const char *commandId = command["id"] | "";
  const size_t commandIdLength =
      strnlen(commandId, sizeof(lastProcessedPostureCommandId));
  if (commandIdLength == 0 ||
      commandIdLength >= sizeof(lastProcessedPostureCommandId) ||
      strcmp(commandId, lastProcessedPostureCommandId) == 0) {
    return;
  }
  strlcpy(lastProcessedPostureCommandId, commandId,
          sizeof(lastProcessedPostureCommandId));
  strlcpy(postureCommandAckId, commandId, sizeof(postureCommandAckId));

  if (!talosWifiMutationSafe(nullptr) ||
      pendingPostureCommand != PostureCommandAction::NONE) {
    strlcpy(postureCommandState, "unsafe",
            sizeof(postureCommandState));
    strlcpy(postureCommandMessage,
            "Calibration requires Balance Off, leg hold off, and neutral outputs.",
            sizeof(postureCommandMessage));
    return;
  }

  const char *actionName = command["action"] | "";
  PostureCommandAction action = PostureCommandAction::NONE;
  if (!strcmp(actionName, "capture-bent")) {
    action = PostureCommandAction::CAPTURE_BENT;
  } else if (!strcmp(actionName, "capture-extended")) {
    action = PostureCommandAction::CAPTURE_EXTENDED;
  } else if (!strcmp(actionName, "bench-hold")) {
    action = PostureCommandAction::BENCH_HOLD;
  } else {
    strlcpy(postureCommandState, "unsupported-action",
            sizeof(postureCommandState));
    strlcpy(postureCommandMessage,
            "Unsupported posture calibration action.",
            sizeof(postureCommandMessage));
    return;
  }

  if (!saveLastPostureCommandId(commandId)) {
    strlcpy(postureCommandState, "storage-error",
            sizeof(postureCommandState));
    strlcpy(postureCommandMessage,
            "Calibration request was not queued because replay guard storage failed.",
            sizeof(postureCommandMessage));
    return;
  }

  strlcpy(pendingPostureCommandId, commandId,
          sizeof(pendingPostureCommandId));
  pendingPostureCommand = action;
  strlcpy(postureCommandState, "queued", sizeof(postureCommandState));
  strlcpy(postureCommandMessage,
          "Calibration queued on the real-time controller.",
          sizeof(postureCommandMessage));
}

int findWifiProfileBySsid(
    const char *ssid,
    TalosWifiManager::ProfileInfo (&profiles)[TalosWifiManager::kMaxProfiles],
    size_t &profileCount) {
  profileCount =
      talosWifiManager.copyProfiles(profiles, TalosWifiManager::kMaxProfiles);
  if (!ssid || ssid[0] == '\0') return -1;
  for (size_t i = 0; i < profileCount; ++i) {
    if (strcmp(profiles[i].ssid, ssid) == 0) return static_cast<int>(i);
  }
  return -1;
}

void recordWifiCommandResult(
    const char *commandId, TalosWifiManager::Result result,
    const char *acceptedMessage = nullptr) {
  strlcpy(wifiCommandAckId, commandId ? commandId : "",
          sizeof(wifiCommandAckId));
  if (result == TalosWifiManager::Result::OK) {
    strlcpy(wifiCommandState, "accepted", sizeof(wifiCommandState));
    strlcpy(wifiCommandMessage,
            acceptedMessage ? acceptedMessage : "Wi-Fi command accepted.",
            sizeof(wifiCommandMessage));
  } else {
    strlcpy(wifiCommandState, TalosWifiManager::resultName(result),
            sizeof(wifiCommandState));
    snprintf(wifiCommandMessage, sizeof(wifiCommandMessage),
             "Wi-Fi command rejected: %s.",
             TalosWifiManager::resultName(result));
  }
}

void processWifiCommand(JsonDocument &control) {
  JsonObjectConst command = control["wifiCommand"].as<JsonObjectConst>();
  if (command.isNull()) return;

  const char *commandId = command["id"] | "";
  const size_t commandIdLength = strnlen(commandId, sizeof(lastProcessedWifiCommandId));
  if (commandIdLength == 0 ||
      commandIdLength >= sizeof(lastProcessedWifiCommandId) ||
      strcmp(commandId, lastProcessedWifiCommandId) == 0) {
    return;
  }
  if (!talosWifiMutationSafe(nullptr)) {
    // Reject in RAM without touching flash while the 500 Hz controller or leg
    // support is active. The dashboard must issue a fresh command after all
    // actuator outputs are off.
    strlcpy(lastProcessedWifiCommandId, commandId,
            sizeof(lastProcessedWifiCommandId));
    strlcpy(deferredWifiReplayGuardId, commandId,
            sizeof(deferredWifiReplayGuardId));
    deferredWifiReplayGuardPending = true;
    strlcpy(wifiCommandAckId, commandId, sizeof(wifiCommandAckId));
    strlcpy(wifiCommandState, "unsafe", sizeof(wifiCommandState));
    strlcpy(
        wifiCommandMessage,
        "Wi-Fi changes require balance and leg support to be safely off.",
        sizeof(wifiCommandMessage));
    return;
  }
  // Persist the command ID before any non-idempotent remove/reorder action.
  // This prevents a retained Firebase command from executing again on reboot.
  if (!saveLastWifiCommandId(commandId)) {
    strlcpy(wifiCommandAckId, commandId, sizeof(wifiCommandAckId));
    strlcpy(wifiCommandState, "storage-error", sizeof(wifiCommandState));
    strlcpy(
        wifiCommandMessage,
        "Wi-Fi command not executed because its replay guard could not be saved.",
        sizeof(wifiCommandMessage));
    return;
  }
  strlcpy(lastProcessedWifiCommandId, commandId,
          sizeof(lastProcessedWifiCommandId));

  const char *action = command["action"] | "";
  const char *ssid = command["ssid"] | "";
  TalosWifiManager::Result result = TalosWifiManager::Result::RADIO_ERROR;
  const char *acceptedMessage = "Wi-Fi command accepted.";

  if (!strcmp(action, "setup")) {
    result = talosWifiManager.requestSetupMode();
    acceptedMessage =
        "Setup AP requested. Join the reported TALOS-SETUP-<MAC4> network.";
  } else if (!strcmp(action, "scan")) {
    result = talosWifiManager.requestScan();
    acceptedMessage = "Nearby network scan started.";
  } else if (!strcmp(action, "reconnect")) {
    if (ssid[0] == '\0') {
      result = talosWifiManager.requestReconnect();
    } else {
      TalosWifiManager::ProfileInfo profiles[TalosWifiManager::kMaxProfiles];
      size_t profileCount = 0;
      const int profileIndex =
          findWifiProfileBySsid(ssid, profiles, profileCount);
      result =
          profileIndex < 0
              ? TalosWifiManager::Result::PROFILE_NOT_FOUND
              : talosWifiManager.requestReconnectToProfile(
                    profiles[profileIndex].id);
    }
    acceptedMessage = "Reconnect to the selected saved network started.";
  } else if (!strcmp(action, "remove")) {
    TalosWifiManager::ProfileInfo profiles[TalosWifiManager::kMaxProfiles];
    size_t profileCount = 0;
    const int profileIndex =
        findWifiProfileBySsid(ssid, profiles, profileCount);
    result =
        profileIndex < 0
            ? TalosWifiManager::Result::PROFILE_NOT_FOUND
            : talosWifiManager.removeProfile(profiles[profileIndex].id);
    acceptedMessage = "Saved network removed.";
  } else if (!strcmp(action, "reorder")) {
    TalosWifiManager::ProfileInfo profiles[TalosWifiManager::kMaxProfiles];
    size_t profileCount = 0;
    const int profileIndex =
        findWifiProfileBySsid(ssid, profiles, profileCount);
    const int direction = command["direction"] | 0;
    const int destination = profileIndex + direction;
    if (profileIndex < 0) {
      result = TalosWifiManager::Result::PROFILE_NOT_FOUND;
    } else if ((direction != -1 && direction != 1) ||
               destination < 0 ||
               destination >= static_cast<int>(profileCount)) {
      result = TalosWifiManager::Result::INVALID_ORDER;
    } else {
      TalosWifiManager::ProfileInfo temporary = profiles[profileIndex];
      profiles[profileIndex] = profiles[destination];
      profiles[destination] = temporary;
      const char *orderedIds[TalosWifiManager::kMaxProfiles] = {};
      for (size_t i = 0; i < profileCount; ++i) {
        orderedIds[i] = profiles[i].id;
      }
      result = talosWifiManager.reorderProfiles(orderedIds, profileCount);
    }
    acceptedMessage = "Saved network priority changed.";
  } else {
    strlcpy(wifiCommandAckId, commandId, sizeof(wifiCommandAckId));
    strlcpy(wifiCommandState, "unsupported-action",
            sizeof(wifiCommandState));
    strlcpy(wifiCommandMessage, "Unsupported Wi-Fi command.",
            sizeof(wifiCommandMessage));
    return;
  }

  recordWifiCommandResult(commandId, result, acceptedMessage);
}

bool readFirebaseControl() {
  const uint32_t requestStartedMs = millis();
  String response;
  const int code = firebaseGet("robot/control", response);
  lastControlLatencyMs = (uint32_t)(millis() - requestStartedMs);
  if (lastControlLatencyMs > maxControlLatencyMs) {
    maxControlLatencyMs = lastControlLatencyMs;
  }
  lastControlHttpCode = code;
  if (code != HTTP_CODE_OK) {
    if (consecutiveControlFailures < UINT32_MAX) {
      ++consecutiveControlFailures;
    }
    return false;
  }

  JsonDocument control;
  if (deserializeJson(control, response)) {
    lastControlHttpCode = -3;
    if (consecutiveControlFailures < UINT32_MAX) {
      ++consecutiveControlFailures;
    }
    return false;
  }

  dashboardEmergencyStopNonce =
      control["emergencyStopNonce"] | dashboardEmergencyStopNonce;
  if (serialControlActive || benchModeActive) {
    if (benchModeActive && !serialControlActive) processJointCommand(control);
    uint64_t revision = control["motionRevision"] | (uint64_t)0;
    if (!localOffBaselineReady) {
      localOffBaselineReady = true;
      localOffBaselineRevision = revision;
    } else if (!(control["armed"] | false) && revision > localOffBaselineRevision) {
      dashboardOffPending = true;
      localOffBaselineRevision = revision;
    }
    lastSuccessfulControlMs = millis();
    consecutiveControlFailures = 0;
    cloudControlStale = false;
    return true;
  }
  processRuntimeSettings(control);
  processWifiCommand(control);

  bool requestedArm = control["armed"] | false;
  if (!requestedArm) {
    dashboardArmRequested = false;
    dashboardArmEdgePending = false;
    dashboardHasSeenOff = true;
    dashboardMotion = DashboardMotion::STOP;
  } else if (dashboardHasSeenOff) {
    if (!dashboardArmRequested) dashboardArmEdgePending = true;
    dashboardArmRequested = true;
  }

  DashboardMotion requestedMotion =
      parseDashboardMotion(control["command"] | "stop");
  const char *mode = control["mode"] | "manual";
  const uint64_t requestedMotionRevision =
      control["motionRevision"] | (uint64_t)0;
  const bool firstValidControl = lastSuccessfulControlMs == 0;
  const bool balanceOnly = !strcmp(mode, "balance-only");

  // A stale cloud link always cancels drive/turn commands. A retained
  // "forward" value in Firebase cannot resume after reconnection until the
  // dashboard writes a newer motion revision.
  if (balanceOnly || requestedMotion == DashboardMotion::STOP) {
    dashboardMotion = DashboardMotion::STOP;
    if (requestedMotion == DashboardMotion::STOP) {
      motionRefreshRequired = false;
    }
  } else if (!firstValidControl &&
             (!motionRefreshRequired ||
              requestedMotionRevision > motionRevisionAtLinkLoss)) {
    dashboardMotion = requestedMotion;
    motionRefreshRequired = false;
  } else {
    dashboardMotion = DashboardMotion::STOP;
  }
  if (requestedMotionRevision > dashboardMotionRevision) {
    dashboardMotionRevision = requestedMotionRevision;
  }
  dashboardSpeedPercent = constrain((int)(control["speed"] | 40), 0, 100);
  legAssistEnabled = control["legAssist"] | false;
  processHeightCommand(control);
  processPostureCommand(control);
  processJointCommand(control);
  lastSuccessfulControlMs = millis();
  consecutiveControlFailures = 0;
  cloudControlStale = false;
  return true;
}

bool pushFirebaseStatus() {
  float liveExtension = calculateRobotExtension();
  float liveHeightCm = calculateRobotHeightCm(liveExtension);
  float targetExtension = calculateTargetRobotExtension();
  float targetHeightCm = calculateRobotHeightCm(targetExtension);
  bool encodersHealthy = leftEncoder.valid && rightEncoder.valid &&
                         leftEncoder.magnetDetected && rightEncoder.magnetDetected &&
                         !legEncoderFaultActive;
  TalosWifiManager::StatusSnapshot wifiStatus{};
  talosWifiManager.getStatus(wifiStatus);
  TalosWifiManager::ProfileInfo
      savedWifiProfiles[TalosWifiManager::kMaxProfiles];
  const size_t savedWifiProfileCount = talosWifiManager.copyProfiles(
      savedWifiProfiles, TalosWifiManager::kMaxProfiles);
  TalosWifiManager::ScanInfo
      wifiScanEntries[TalosWifiManager::kMaxScanResults];
  const size_t wifiScanEntryCount = talosWifiManager.copyScanResults(
      wifiScanEntries, TalosWifiManager::kMaxScanResults);

  JsonDocument status;
  status["online"] = true;
  status["firmware"] = "talos-stable-control-v17";
  status["controlOwner"] = serialControlActive ? "serial" : "dashboard";
  status["measuredControlHz"] = measuredControlHz;
  status["servoFrameHz"] = 50;
  status["poseStreamSupported"] = true;
  status["poseBootId"] = poseStreamBootId;
  status["imuSampleHealthy"] = imuFaultStreak == 0 && (uint32_t)(micros() - lastImuSampleUs) < IMU_STALE_LIMIT_US;
  status["heightEstimateValid"] = heightCalibrationValid;
  status["heightModel"] = heightCalibrationValid ? "two-point-estimate" : "uncalibrated";
  status["heightMotionPaused"] = heightMotionPaused;
  status["wheelCommandMode"] = "smooth-pid";
  status["jointControlSupported"] = true;
  status["benchModeActive"] = benchModeActive;
  status["leftBendDeg"] = extendedHeightCaptured ? measuredBendDeg(true) : 0.0f;
  status["rightBendDeg"] = extendedHeightCaptured ? measuredBendDeg(false) : 0.0f;
  status["leftBendTargetDeg"] = extendedHeightCaptured
      ? LEFT_LEG_ENCODER_SIGN * wrap180(leftExtendedDeg - leftRequestedJointDeg) : 0.0f;
  status["rightBendTargetDeg"] = extendedHeightCaptured
      ? RIGHT_LEG_ENCODER_SIGN * wrap180(rightExtendedDeg - rightRequestedJointDeg) : 0.0f;
  status["leftBendLimitDeg"] = maximumBendDeg(true);
  status["rightBendLimitDeg"] = maximumBendDeg(false);
  status["leftExtendedEncoderDeg"] = leftExtendedDeg;
  status["rightExtendedEncoderDeg"] = rightExtendedDeg;
  status["leftEncoderSign"] = LEFT_LEG_ENCODER_SIGN;
  status["rightEncoderSign"] = RIGHT_LEG_ENCODER_SIGN;
  status["leftEncoderSampleAgeMs"] = leftEncoder.lastReadUs ? (uint32_t)(micros() - leftEncoder.lastReadUs) / 1000 : 9999;
  status["rightEncoderSampleAgeMs"] = rightEncoder.lastReadUs ? (uint32_t)(micros() - rightEncoder.lastReadUs) / 1000 : 9999;
  status["jointCommandState"] = jointCommandState;
  status["jointCommandMessage"] = jointCommandMessage;
  status["jointCommandAckRevision"] = jointCommandAckRevision;
  status["jointSpeedDps"] = jointSpeedDps;
  status["state"] = sensorFault ? "sensor-fault" : (armed ? "armed" : "disarmed");
  status["autoState"] = serialControlActive ? "serial-control" : autoStateName(autoStartupState);
  status["emergencyStopActive"] =
      (int32_t)(emergencyStopActiveUntilMs - millis()) > 0;
  status["tilt"] = angleDeg;
  status["predictedAngle"] = predictedAngleDeg;
  status["roll"] = rollDeg;
  status["gyroY"] = gyroRateDps;
  status["rollRate"] = rollRateDps;
  status["yawRate"] = yawRateDps;
  status["accelMagnitudeG"] = accelMagG;
  status["pidOutput"] = pidOutput;
  status["balanceAngle"] = BALANCE_ANGLE;
  status["balanceTrimDeg"] = BALANCE_TRIM_DEG;
  status["balanceTargetAngle"] = BALANCE_ANGLE + BALANCE_TRIM_DEG;
  status["angleError"] = angleDeg - (BALANCE_ANGLE + BALANCE_TRIM_DEG);
  status["fallAngleLimitDeg"] = FALL_ANGLE_DEG;
  status["kp"] = KP;
  status["ki"] = KI;
  status["kd"] = KD;
  status["predictionTimeMs"] = BALANCE_PREDICTION_TIME_S * 1000.0f;
  status["maxWheelOutputUs"] = MAX_WHEEL_OUTPUT_US;
  status["minActiveWheelOutputUs"] = WHEEL_MIN_ACTIVE_US;
  status["fullPowerErrorDeg"] = currentFullPowerErrorDeg;
  status["scheduledFullPowerErrorDeg"] = currentFullPowerErrorDeg;
  status["scheduledFastTiltErrorDeg"] = currentFastTiltErrorDeg;
  status["wheelAuthorityPercent"] = currentWheelAuthorityPercent;
  status["scheduledWheelMaxOutputUs"] = currentWheelMaxOutputUs;
  status["scheduledWheelSlewUs"] = currentWheelSlewUs;
  status["settingsAckRevision"] = settingsAckRevision;
  status["settingsAck"] = settingsAckName(settingsAck);
  JsonObject tuningStatus = status["tuning"].to<JsonObject>();
  tuningStatus["kp"] = KP;
  tuningStatus["ki"] = KI;
  tuningStatus["kd"] = KD;
  tuningStatus["balanceTrimDeg"] = BALANCE_TRIM_DEG;
  tuningStatus["predictionTimeMs"] =
      BALANCE_PREDICTION_TIME_S * 1000.0f;
  tuningStatus["maxWheelOutputUs"] = MAX_WHEEL_OUTPUT_US;
  tuningStatus["minActiveWheelOutputUs"] = WHEEL_MIN_ACTIVE_US;
  status["lastStopReason"] = balanceStopReasonName(lastBalanceStopReason);
  status["lastStopAngleDeg"] = lastBalanceStopAngleDeg;
  status["lastStopUptimeMs"] = lastBalanceStopMs;
  status["balancingEnabled"] = armed;
  status["armRequested"] = serialControlActive ? armed : dashboardArmRequested;
  status["armEdgePending"] = dashboardArmEdgePending;
  status["standingEnabled"] = standingEnabled;
  status["legAssistEnabled"] = legAssistEnabled;
  status["wifiRssi"] = wifiStatus.rssi;
  status["wifiSsid"] = wifiStatus.currentSsid;
  status["ipAddress"] = wifiStatus.ipAddress;
  status["wifiState"] =
      TalosWifiManager::stateName(wifiStatus.state);
  status["wifiProvisioningState"] =
      wifiStatus.setupActive
          ? "setup-active"
          : TalosWifiManager::stateName(wifiStatus.state);
  status["wifiSetupActive"] = wifiStatus.setupActive;
  status["wifiSetupSsid"] = wifiStatus.setupSsid;
  status["wifiSetupIpAddress"] = wifiStatus.setupIpAddress;
  status["wifiScanRunning"] = wifiStatus.scanRunning;
  status["wifiProfileCount"] = wifiStatus.profileCount;
  status["wifiProfilesRevision"] = wifiStatus.profilesRevision;
  status["wifiLastResult"] = wifiStatus.lastResult;
  status["wifiCommandId"] = wifiCommandAckId;
  status["wifiCommandAckId"] = wifiCommandAckId;
  status["wifiCommandState"] = wifiCommandState;
  status["wifiCommandMessage"] = wifiCommandMessage;
  JsonArray wifiProfiles = status["wifiProfiles"].to<JsonArray>();
  for (size_t i = 0; i < savedWifiProfileCount; ++i) {
    JsonObject profile = wifiProfiles.add<JsonObject>();
    profile["id"] = savedWifiProfiles[i].id;
    profile["ssid"] = savedWifiProfiles[i].ssid;
    profile["priority"] = savedWifiProfiles[i].priority + 1;
    profile["verified"] = savedWifiProfiles[i].verified;
    profile["active"] = savedWifiProfiles[i].active;
    if (savedWifiProfiles[i].active) profile["rssi"] = wifiStatus.rssi;
  }
  JsonArray wifiScanResults =
      status["wifiScanResults"].to<JsonArray>();
  for (size_t i = 0; i < wifiScanEntryCount; ++i) {
    JsonObject network = wifiScanResults.add<JsonObject>();
    network["ssid"] = wifiScanEntries[i].ssid;
    network["rssi"] = wifiScanEntries[i].rssi;
    network["channel"] = wifiScanEntries[i].channel;
    network["authMode"] = wifiScanEntries[i].authMode;
  }
  status["wheelLeftUs"] = leftWheelPulse;
  status["wheelRightUs"] = rightWheelPulse;
  status["legLeftUs"] = leftLegPulse;
  status["legRightUs"] = rightLegPulse;
  status["heightCm"] = liveHeightCm;
  status["targetHeightCm"] = targetHeightCm;
  status["requestedHeightCm"] =
      calculateRobotHeightCm(requestedHeightExtension);
  status["extensionPercent"] = liveExtension * 100.0f;
  status["targetExtensionPercent"] = targetExtension * 100.0f;
  status["requestedExtensionPercent"] =
      requestedHeightExtension * 100.0f;
  status["scheduledExtensionPercent"] =
      fmaxf(liveExtension, targetExtension) * 100.0f;
  status["dynamicCrouchCm"] =
      dynamicCrouchExtension *
      (MAX_ROBOT_HEIGHT_CM - MIN_ROBOT_HEIGHT_CM);
  status["legMotionSpeedPercent"] = legMotionSpeedPercent;
  status["heightCommandRevision"] = dashboardHeightCommandRevision;
  status["heightCommandAckRevision"] = heightCommandAckRevision;
  status["heightCommandState"] = heightCommandState;
  status["heightCommandMessage"] = heightCommandMessage;
  status["leftMagneticStatus"] = leftEncoder.magneticStatus;
  status["rightMagneticStatus"] = rightEncoder.magneticStatus;
  status["encoderMagnetWarning"] =
      ((leftEncoder.magneticStatus | rightEncoder.magneticStatus) & 0x18) != 0;
  status["encoderHealthMessage"] = !leftEncoder.magnetDetected || !rightEncoder.magnetDetected
      ? "Magnet missing: check wiring, magnet type, and centering."
      : ((leftEncoder.magneticStatus | rightEncoder.magneticStatus) & 0x10)
          ? "Magnetic field too weak: center diametric magnets and reduce sensor gap."
          : ((leftEncoder.magneticStatus | rightEncoder.magneticStatus) & 0x08)
              ? "Magnetic field too strong: increase sensor gap slightly."
              : "Magnetic field within sensor limits.";
  status["leftEncoderExtensionSign"] = LEFT_LEG_ENCODER_SIGN;
  status["rightEncoderExtensionSign"] = RIGHT_LEG_ENCODER_SIGN;
  status["leftEncoderDeg"] = leftEncoder.angleDeg;
  status["rightEncoderDeg"] = rightEncoder.angleDeg;
  status["leftEncoderVelocityDps"] = leftEncoder.velocityDps;
  status["rightEncoderVelocityDps"] = rightEncoder.velocityDps;
  status["leftLegTargetDeg"] = standingEnabled ? leftActualTargetDeg : leftStandTargetDeg;
  status["rightLegTargetDeg"] = standingEnabled ? rightActualTargetDeg : rightStandTargetDeg;
  status["leftLegErrorDeg"] = leftLegFilteredErrorDeg;
  status["rightLegErrorDeg"] = rightLegFilteredErrorDeg;
  status["leftLegPredictedErrorDeg"] = leftLegPredictedErrorDeg;
  status["rightLegPredictedErrorDeg"] = rightLegPredictedErrorDeg;
  status["leftLegDriveState"] = legDriveStateName(leftLegDriveState);
  status["rightLegDriveState"] = legDriveStateName(rightLegDriveState);
  status["leftEncoderRaw"] = leftEncoder.raw;
  status["rightEncoderRaw"] = rightEncoder.raw;
  status["leftEncoderMagnet"] = leftEncoder.magnetDetected;
  status["rightEncoderMagnet"] = rightEncoder.magnetDetected;
  status["encodersHealthy"] = encodersHealthy;
  status["heightCalibrationValid"] = heightCalibrationValid;
  status["bentHeightCaptured"] = bentHeightCaptured;
  status["extendedHeightCaptured"] = extendedHeightCaptured;
  status["postureCommandAckId"] = postureCommandAckId;
  status["postureCommandState"] = postureCommandState;
  status["postureCommandMessage"] = postureCommandMessage;
  status["effectiveKp"] = currentWheelKp;
  status["effectiveKd"] = currentWheelKd;
  status["loopTimeUs"] = measuredControlHz > 0.0f ? (int)lroundf(1000000.0f / measuredControlHz) : 0;
  status["controlRateHz"] = 1000000UL / CONTROL_PERIOD_US;
  status["controlLoopCount"] = controlLoopCount;
  status["loopOverruns"] = controlLoopOverruns;
  status["maxControlLatenessUs"] = maximumControlLatenessUs;
  status["imuErrors"] = imuErrors;
  status["encoderErrors"] = encoderErrors;
  status["lastCommand"] = dashboardMotionName(dashboardMotion);
  status["speedPercent"] = dashboardSpeedPercent;
  status["transport"] = "wifi";
  const uint32_t controlAgeMs =
      lastSuccessfulControlMs == 0
          ? 0
          : (uint32_t)(millis() - lastSuccessfulControlMs);
  status["networkFresh"] =
      lastSuccessfulControlMs != 0 &&
      controlAgeMs <= CLOUD_CONTROL_STALE_MS;
  status["cloudControlStale"] = cloudControlStale;
  status["cloudControlAgeMs"] = controlAgeMs;
  status["cloudControlStaleAfterMs"] = CLOUD_CONTROL_STALE_MS;
  status["lastControlHttpCode"] = lastControlHttpCode;
  status["lastControlLatencyMs"] = lastControlLatencyMs;
  status["maxControlLatencyMs"] = maxControlLatencyMs;
  status["consecutiveControlFailures"] = consecutiveControlFailures;
  status["lastStatusHttpCode"] = lastStatusHttpCode;
  status["lastStatusLatencyMs"] = lastStatusLatencyMs;
  status["motionRefreshRequired"] = motionRefreshRequired;
  status["motionRevision"] = dashboardMotionRevision;
  JsonObject timestamp = status["updatedAt"].to<JsonObject>();
  timestamp[".sv"] = "timestamp";

  String body;
  serializeJson(status, body);
  const uint32_t requestStartedMs = millis();
  int code = firebasePatch("robot/status", body);
  lastStatusLatencyMs = (uint32_t)(millis() - requestStartedMs);
  lastStatusHttpCode = code;
  return code >= 200 && code < 300;
}

bool pushFirebasePose() {
  JsonDocument pose;
  pose["online"] = true;
  pose["firmware"] = "talos-stable-control-v17";
  pose["poseBootId"] = poseStreamBootId;
  pose["tilt"] = angleDeg;
  pose["roll"] = rollDeg;
  pose["imuErrors"] = imuErrors;
  pose["imuSampleHealthy"] = imuFaultStreak == 0 && (uint32_t)(micros() - lastImuSampleUs) < IMU_STALE_LIMIT_US;
  pose["leftEncoderDeg"] = leftEncoder.angleDeg;
  pose["rightEncoderDeg"] = rightEncoder.angleDeg;
  pose["leftMagneticStatus"] = leftEncoder.magneticStatus;
  pose["rightMagneticStatus"] = rightEncoder.magneticStatus;
  pose["leftEncoderSampleAgeMs"] = leftEncoder.lastReadUs ? (uint32_t)(micros() - leftEncoder.lastReadUs) / 1000 : 9999;
  pose["rightEncoderSampleAgeMs"] = rightEncoder.lastReadUs ? (uint32_t)(micros() - rightEncoder.lastReadUs) / 1000 : 9999;
  pose["leftLegTargetDeg"] = standingEnabled ? leftActualTargetDeg : leftStandTargetDeg;
  pose["rightLegTargetDeg"] = standingEnabled ? rightActualTargetDeg : rightStandTargetDeg;
  pose["leftBendDeg"] = extendedHeightCaptured ? measuredBendDeg(true) : 0.0f;
  pose["rightBendDeg"] = extendedHeightCaptured ? measuredBendDeg(false) : 0.0f;
  pose["leftBendTargetDeg"] = extendedHeightCaptured
      ? LEFT_LEG_ENCODER_SIGN * wrap180(leftExtendedDeg - leftRequestedJointDeg) : 0.0f;
  pose["rightBendTargetDeg"] = extendedHeightCaptured
      ? RIGHT_LEG_ENCODER_SIGN * wrap180(rightExtendedDeg - rightRequestedJointDeg) : 0.0f;
  pose["benchModeActive"] = benchModeActive;
  pose["balancingEnabled"] = armed;
  pose["standingEnabled"] = standingEnabled;
  pose["encodersHealthy"] = leftEncoder.valid && rightEncoder.valid && !legEncoderFaultActive;
  pose["jointCommandState"] = jointCommandState;
  pose["jointCommandMessage"] = jointCommandMessage;
  pose["legLeftUs"] = leftLegPulse;
  pose["legRightUs"] = rightLegPulse;
  pose["wheelLeftUs"] = leftWheelPulse;
  pose["wheelRightUs"] = rightWheelPulse;
  pose["measuredControlHz"] = measuredControlHz;
  pose["updatedAt"][".sv"] = "timestamp";
  String body;
  serializeJson(pose, body);
  return firebasePatch("robot/status", body) == HTTP_CODE_OK;
}

void firebaseTask(void *parameter) {
  uint32_t lastPollMs = 0;
  uint32_t lastPushMs = 0;
  uint32_t lastPosePushMs = 0;
  uint32_t lastManagerBeginAttemptMs = millis();
  talosWifiManager.setSafetyGate(talosWifiMutationSafe);
  bool wifiManagerStarted = talosWifiManager.begin();
  if (!wifiManagerStarted) {
    Serial.println("Wi-Fi manager failed to initialize; retrying on Core 0.");
  }

  for (;;) {
    uint32_t now = millis();
    if (!wifiManagerStarted &&
        now - lastManagerBeginAttemptMs >= 5000) {
      lastManagerBeginAttemptMs = now;
      wifiManagerStarted = talosWifiManager.begin();
    }
    if (wifiManagerStarted) talosWifiManager.service();
    if (wifiManagerStarted && pendingLocalWifiAdd && talosWifiMutationSafe(nullptr)) {
      const auto result = talosWifiManager.saveAndTestProfile(
          pendingLocalWifiSsid, pendingLocalWifiPassword, 0);
      if (result != TalosWifiManager::Result::BUSY) {
        pendingLocalWifiAdd = false;
        memset(pendingLocalWifiPassword, 0, sizeof(pendingLocalWifiPassword));
        Serial.printf("Local Wi-Fi request: %s for %s. Password not logged.\n",
                      TalosWifiManager::resultName(result), pendingLocalWifiSsid);
        pendingLocalWifiSsid[0] = '\0';
      }
    }
    if (deferredWifiReplayGuardPending &&
        talosWifiMutationSafe(nullptr) &&
        saveLastWifiCommandId(deferredWifiReplayGuardId)) {
      deferredWifiReplayGuardPending = false;
      deferredWifiReplayGuardId[0] = '\0';
    }
    if (talosWifiManager.isConnected()) {
      if (now - lastPollMs >= FIREBASE_CONTROL_POLL_MS) {
        lastPollMs = now;
        readFirebaseControl();
      }
      if (talosWifiManager.isConnected() &&
          now - lastPushMs >= FIREBASE_STATUS_PUSH_MS) {
        lastPushMs = now;
        pushFirebaseStatus();
        lastPosePushMs = millis();
      } else if ((uint32_t)(millis() - lastPosePushMs) >= FIREBASE_POSE_PUSH_MS) {
        pushFirebasePose();
        lastPosePushMs = millis();
      }
    }

    const uint32_t safetyNow = millis();
    const bool controlIsStale =
        lastSuccessfulControlMs == 0 ||
        (uint32_t)(safetyNow - lastSuccessfulControlMs) >
            CLOUD_CONTROL_STALE_MS;
    if (controlIsStale && !cloudControlStale) {
      motionRevisionAtLinkLoss = dashboardMotionRevision;
      motionRefreshRequired = true;
    }
    cloudControlStale = controlIsStale;
    if (controlIsStale) {
      // Do not deliberately make an upright robot fall because Firebase or the
      // hotspot paused. Remove remote lean/turn immediately, but let an
      // already-armed controller keep balancing from local sensors. A pending
      // (not-yet-armed) request is cancelled and must receive a fresh OFF->ON.
      dashboardMotion = DashboardMotion::STOP;
      if (!armed) {
        dashboardArmRequested = false;
        dashboardArmEdgePending = false;
        dashboardHasSeenOff = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void serviceAutomaticStatusLed() {
  static uint32_t lastToggleMs = 0;
  static bool ledOn = false;

  if (autoStartupState == AutoStartupState::RUNNING && armed) {
    digitalWrite(STATUS_LED_PIN, HIGH);  // solid: balancing is active
    ledOn = true;
    return;
  }

  uint32_t intervalMs =
      autoStartupState == AutoStartupState::FAULT ? 120 : 450;

  if ((uint32_t)(millis() - lastToggleMs) >= intervalMs) {
    lastToggleMs = millis();
    ledOn = !ledOn;
    digitalWrite(STATUS_LED_PIN, ledOn ? HIGH : LOW);
  }
}

void serviceAutomaticStartup() {
  if (dashboardEmergencyStopNonce != 0 &&
      dashboardEmergencyStopNonce != lastProcessedEmergencyStopNonce) {
    lastProcessedEmergencyStopNonce = dashboardEmergencyStopNonce;
    beginEmergencyStop(BalanceStopReason::MANUAL_STOP);
  }
  if (dashboardOffPending) {
    dashboardOffPending = false;
    beginEmergencyStop(BalanceStopReason::DASHBOARD_OFF);
  }
  if (serialControlActive || benchModeActive) return;
  if (!dashboardArmRequested) {
    if (armed || standingEnabled || wheelManualTestActive) beginDashboardOff();
    if (autoStartupState != AutoStartupState::WAIT_INITIAL_DELAY) {
      enterAutoState(AutoStartupState::WAIT_INITIAL_DELAY);
    }
    return;
  }
  if ((int32_t)(millis() - startupCompleteMs) < 0 ||
      (int32_t)(emergencyStopActiveUntilMs - millis()) > 0) return;
  if (sensorFault) {
    beginEmergencyStop(BalanceStopReason::IMU_FAULT);
    enterAutoState(AutoStartupState::FAULT);
    return;
  }
  if (armed) {
    if (!standingEnabled) beginEmergencyStop(BalanceStopReason::LEG_HOLD_FAULT);
    return;
  }
  if (!dashboardArmEdgePending || !automaticStableFor(AUTO_REARM_STABLE_MS)) return;
  leftStandTargetDeg = leftEncoder.angleDeg;
  rightStandTargetDeg = rightEncoder.angleDeg;
  standPoseCaptured = true;
  enableStandingHold();
  pidIntegral = 0.0f;
  pidOutput = 0.0f;
  dashboardArmEdgePending = false;
  lastBalanceStopReason = BalanceStopReason::NONE;
  armed = true;
  enterAutoState(AutoStartupState::RUNNING);
  Serial.println("DASHBOARD ON: captured current leg pose; balancing with saved upright zero.");
}

void startWheelManualTest(int leftSignedCommandUs, int rightSignedCommandUs) {
  armed = false;
  pidIntegral = 0.0f;
  pidOutput = 0.0f;
  leftWheelManualCommandUs = constrain(leftSignedCommandUs, -MAX_WHEEL_OUTPUT_US, MAX_WHEEL_OUTPUT_US);
  rightWheelManualCommandUs = constrain(rightSignedCommandUs, -MAX_WHEEL_OUTPUT_US, MAX_WHEEL_OUTPUT_US);
  wheelManualUntilMs = millis() + 250;
  wheelManualTestActive = true;
  Serial.printf("Wheel test: left=%+d right=%+d for 250 ms. Keep TALOS supported with wheels raised.\n", leftWheelManualCommandUs, rightWheelManualCommandUs);
}

void stopWheelManualTest() {
  wheelManualTestActive = false;
  leftWheelManualCommandUs = 0;
  rightWheelManualCommandUs = 0;
  neutralWheels();
  Serial.println("Wheel test stopped.");
}

bool recoverIMU() {
  armed = false;
  wheelManualTestActive = false;
  leftWheelManualCommandUs = 0;
  rightWheelManualCommandUs = 0;
  neutralWheels();
  Serial.println("Reinitializing MPU6050. Keep TALOS completely still...");
  sensorFault = false;
  imuFaultStreak = 0;
  if (!initializeMPU()) {
    sensorFault = true;
    Serial.println("IMU recovery failed: MPU6050 did not acknowledge on I2C.");
    return false;
  }
  calibrateMPU();
  if (sensorFault) {
    Serial.println("IMU recovery failed during calibration.");
    return false;
  }
  imuFaultStreak = 0;
  Serial.println("IMU recovered. You may send 'arm' while holding TALOS upright.");
  return true;
}

// ---------------------------- Serial console -------------------------------
void printHelp() {
  Serial.println("\nTALOS V17 - supported commissioning only");
  Serial.println("stop / disarm: neutral all four servos immediately");
  Serial.println("local / remote: select serial or dashboard control");
  Serial.println("status / pose / magnet-status / wifi-status / telemetry-on / telemetry-off");
  Serial.println("wifi-add JSON: local USB provisioning with ssid and password; all motors must be off");
  Serial.println("stand: save current leg pose, motors OFF; balance zero unchanged");
  Serial.println("bench-hold / bend <left> <right> <deg/s>: supported leg-only angle control; wheels off");
  Serial.println("hold: hold captured leg pose; arm / arm-saved: use saved upright zero");
  Serial.println("zero: explicitly save upright balance zero, supported with motors OFF");
  Serial.println("capture-bent / capture-extended: supported 16.2 / 29.2 cm calibration");
  Serial.println("set heightcm <16.2..29.2> / set heightpct <0..100> / set legspeed <0..100>");
  Serial.println("set kp/ki/kd/balance/controlsign/legkp/legki/legkd/leftbias/rightbias/cutoff <value>");
  Serial.println("assist-on/off: bounded differential roll and crouch assistance");
  Serial.println("wheel-forward/reverse or left-/right-wheel-forward/reverse: +/-100 us, 250 ms ONLY");
  Serial.println("imu-reset / save: motors OFF only; leg gain edits are RAM-only");
}

void printStatus() {
  updateHeightScheduledWheelParameters(armed ? fabsf(angleDeg - BALANCE_ANGLE) : 0.0f);
  float liveExtension = calculateRobotExtension();
  float liveHeightCm = calculateRobotHeightCm(liveExtension);
  float targetExtension = calculateTargetRobotExtension();
  float targetHeightCm = calculateRobotHeightCm(targetExtension);
  Serial.printf("state=%s stand=%s wheelTest=%s angle=%.2f wheelPulse=%d/%d\n",
                armed ? "armed" : "disarmed", standingEnabled ? "on" : "off",
                wheelManualTestActive ? "on" : "off", angleDeg, leftWheelPulse, rightWheelPulse);
  Serial.printf("heightLive=%.2fcm liveExt=%.1f%% scheduledHeight=%.2fcm targetExt=%.1f%% heightCal=%s bent=%.2f/%.2f extended=%.2f/%.2f\n",
                liveHeightCm, liveExtension * 100.0f, targetHeightCm, targetExtension * 100.0f,
                heightCalibrationValid ? "ok" : "missing", leftBentDeg, rightBentDeg, leftExtendedDeg, rightExtendedDeg);
  Serial.printf("wheel baseKP=%.2f baseKD=%.2f effectiveKP=%.2f effectiveKD=%.2f maxOut=%d authority=%.0f%% slew=%d fullAt=%.2f pid=%.1f controlSign=%d balance=%.2f err=%+.2f\n",
                 KP, KD, currentWheelKp, currentWheelKd,
                 currentWheelMaxOutputUs, currentWheelAuthorityPercent,
                 currentWheelSlewUs, currentFullPowerErrorDeg, pidOutput,
                 WHEEL_CONTROL_SIGN, BALANCE_ANGLE, angleDeg - BALANCE_ANGLE);
  float leftError = (float)LEFT_LEG_ENCODER_SIGN * wrap180(leftStandTargetDeg - leftEncoder.angleDeg);
  float rightError = (float)RIGHT_LEG_ENCODER_SIGN * wrap180(rightStandTargetDeg - rightEncoder.angleDeg);
  Serial.printf("encL=%.2f->%.2f err=%+.2f filt=%+.2f raw=%u magnet=%d pulse=%d | encR=%.2f->%.2f err=%+.2f filt=%+.2f raw=%u magnet=%d pulse=%d\n",
                leftEncoder.angleDeg, leftStandTargetDeg, leftError, leftLegFilteredErrorDeg, leftEncoder.raw,
                leftEncoder.magnetDetected, leftLegPulse, rightEncoder.angleDeg, rightStandTargetDeg, rightError,
                rightLegFilteredErrorDeg, rightEncoder.raw, rightEncoder.magnetDetected, rightLegPulse);
  Serial.printf("legMode=target-centered KP=%.2f KI=%.2f KD=%.2f support=%d/%d state=%s/%s predicted=%+.2f/%+.2f velocity=%+.1f/%+.1f max=%d/%d captured=%s assist=%s crouch=%.2fcm encFault=%s streak=%u totalErr=%lu\n",
                LEG_KP, LEG_KI, LEG_KD,
                LEFT_LEG_HOLD_BIAS_US, RIGHT_LEG_HOLD_BIAS_US,
                legDriveStateName(leftLegDriveState),
                legDriveStateName(rightLegDriveState),
                leftLegPredictedErrorDeg, rightLegPredictedErrorDeg,
                leftEncoder.velocityDps, rightEncoder.velocityDps,
                LEFT_LEG_MAX_SUPPORT_OFFSET_US, RIGHT_LEG_MAX_SUPPORT_OFFSET_US,
                standPoseCaptured ? "yes" : "no", legAssistEnabled ? "on" : "off",
                dynamicCrouchExtension *
                    (MAX_ROBOT_HEIGHT_CM - MIN_ROBOT_HEIGHT_CM),
                legEncoderFaultActive ? "yes" : "no", legFaultStreak, (unsigned long)encoderErrors);
  Serial.printf("imuFault=%s imuStreak=%u imuTotalErr=%lu\n", sensorFault ? "yes" : "no", imuFaultStreak, (unsigned long)imuErrors);
  Serial.printf("firmware=V17 owner=%s loop=%.1fHz overruns=%lu maxLate=%luus cutoff=%.0f stop=%s stopAngle=%.2f heightEstimate=%s\n",
      serialControlActive ? "serial" : "dashboard", measuredControlHz,
      (unsigned long)controlLoopOverruns, (unsigned long)maximumControlLatenessUs,
      FALL_ANGLE_DEG, balanceStopReasonName(lastBalanceStopReason),
      (float)lastBalanceStopAngleDeg, heightCalibrationValid ? "two-point" : "UNKNOWN");
  Serial.printf("bench=%d bend=%.2f/%.2f targets=%.2f/%.2f limits=%.1f/%.1f joint=%s\n",
      benchModeActive, extendedHeightCaptured ? measuredBendDeg(true) : 0.0f,
      extendedHeightCaptured ? measuredBendDeg(false) : 0.0f,
      LEFT_LEG_ENCODER_SIGN * wrap180(leftExtendedDeg - leftRequestedJointDeg),
      RIGHT_LEG_ENCODER_SIGN * wrap180(rightExtendedDeg - rightRequestedJointDeg),
      maximumBendDeg(true), maximumBendDeg(false), jointCommandState);
  TalosWifiManager::StatusSnapshot wifiStatus{};
  talosWifiManager.getStatus(wifiStatus);
  Serial.printf("wifi=%s profiles=%u RSSI=%ld cloudHttp=%ld controlAge=%lums\n",
      TalosWifiManager::stateName(wifiStatus.state), wifiStatus.profileCount,
      (long)wifiStatus.rssi, (long)lastControlHttpCode,
      (unsigned long)(millis() - lastSuccessfulControlMs));
}

void printCompactStatus() {
  Serial.printf("V17 %s H=%s A=%.2f E=%+.2f W=%d/%d L=%d/%d Hz=%.0f stop=%s imu=%u enc=%u\n",
      armed ? "ARM" : "OFF", standingEnabled ? "ON" : "OFF",
      angleDeg, angleDeg - BALANCE_ANGLE - BALANCE_TRIM_DEG,
      leftWheelPulse, rightWheelPulse, leftLegPulse, rightLegPulse,
      measuredControlHz, balanceStopReasonName(lastBalanceStopReason),
      imuFaultStreak, legFaultStreak);
}

void takeSerialControl() {
  if (!serialControlActive) {
    localOffBaselineRevision = dashboardMotionRevision;
    localOffBaselineReady = lastSuccessfulControlMs != 0;
    serialControlActive = true;
    dashboardMotion = DashboardMotion::STOP;
  }
}

bool allActuatorsOff() {
  return !armed && !standingEnabled && !wheelManualTestActive &&
      leftWheelPulse == LEFT_WHEEL_NEUTRAL_US &&
      rightWheelPulse == RIGHT_WHEEL_NEUTRAL_US &&
      leftLegPulse == LEFT_LEG_NEUTRAL_US &&
      rightLegPulse == RIGHT_LEG_NEUTRAL_US;
}

bool parseSetCommand(const String &command) {
  int firstSpace = command.indexOf(' ');
  int secondSpace = command.indexOf(' ', firstSpace + 1);
  if (firstSpace < 0 || secondSpace < 0) return false;
  String key = command.substring(firstSpace + 1, secondSpace);
  String valueText = command.substring(secondSpace + 1);
  valueText.trim();
  char *end = nullptr;
  float value = strtof(valueText.c_str(), &end);
  if (end == valueText.c_str() || *end != '\0' || !isfinite(value)) return false;
  bool postureSetting = key == "heightcm" || key == "heightpct" || key == "legspeed";
  if (!postureSetting && !allActuatorsOff()) {
    Serial.println("Tuning refused while active; send stop first.");
    return false;
  }
  if (postureSetting && !serialControlActive && (armed || standingEnabled)) return false;
  takeSerialControl();
  if (key == "balance" && fabsf(value) <= 15.0f) BALANCE_ANGLE = value;
  else if (key == "kp" && value >= MIN_KP && value <= MAX_KP) KP = value;
  else if (key == "ki" && value >= MIN_KI && value <= MAX_KI) KI = value;
  else if (key == "kd" && value >= MIN_KD && value <= MAX_KD) KD = value;
  else if (key == "controlsign" && (value == -1.0f || value == 1.0f)) WHEEL_CONTROL_SIGN = (int)value;
  else if (key == "legkp" && value >= 1.0f && value <= 60.0f) LEG_KP = value;
  else if (key == "legki" && value >= 0.0f && value <= 2.0f) LEG_KI = value;
  else if (key == "legkd" && value >= 0.0f && value <= 5.0f) LEG_KD = value;
  else if (key == "leftbias" && value >= -180.0f && value <= -20.0f) LEFT_LEG_HOLD_BIAS_US = (int)value;
  else if (key == "rightbias" && value >= 50.0f && value <= 320.0f) RIGHT_LEG_HOLD_BIAS_US = (int)value;
  else if (key == "cutoff" && isAllowedFallAngle((int)value) && value == (int)value) FALL_ANGLE_DEG = value;
  else if (key == "heightcm") {
    if (!standingEnabled || !validateHeightCalibration() ||
        value < MIN_ROBOT_HEIGHT_CM || value > MAX_ROBOT_HEIGHT_CM) return false;
    requestedHeightExtension = calculateExtensionForHeightCm(value);
  } else if (key == "heightpct") {
    if (!standingEnabled || !validateHeightCalibration() || value < 0.0f || value > 100.0f) return false;
    requestedHeightExtension = value / 100.0f;
  } else if (key == "legspeed" && value >= 0.0f && value <= 100.0f) {
    legMotionSpeedPercent = (int)lroundf(value);
  } else return false;
  Serial.printf("%s set to %.3f\n", key.c_str(), value);
  return true;
}

void printPose() {
  JsonDocument pose;
  pose["pose"] = true;
  pose["ms"] = millis();
  pose["left"] = leftEncoder.angleDeg;
  pose["right"] = rightEncoder.angleDeg;
  pose["leftBend"] = extendedHeightCaptured ? measuredBendDeg(true) : 0.0f;
  pose["rightBend"] = extendedHeightCaptured ? measuredBendDeg(false) : 0.0f;
  pose["targetLeft"] = leftActualTargetDeg;
  pose["targetRight"] = rightActualTargetDeg;
  pose["leftPulse"] = leftLegPulse;
  pose["rightPulse"] = rightLegPulse;
  pose["wheelLeft"] = leftWheelPulse;
  pose["wheelRight"] = rightWheelPulse;
  pose["healthy"] = leftEncoder.valid && rightEncoder.valid && !sensorFault;
  pose["bench"] = benchModeActive;
  pose["joint"] = jointCommandState;
  pose["stop"] = balanceStopReasonName(lastBalanceStopReason);
  serializeJson(pose, Serial);
  Serial.println();
}

void servicePoseStream() {
  if (!poseStreamEnabled) return;
  if ((int32_t)(millis() - poseStreamDeadlineMs) >= 0) {
    poseStreamEnabled = false;
    return;
  }
  uint32_t nowUs = micros();
  if ((int32_t)(nowUs - nextPoseStreamUs) < 0) return;
  nextPoseStreamUs = nowUs + 40000;
  if (Serial.availableForWrite() < 3584) return;
  const uint32_t leftAgeMs = leftEncoder.lastReadUs ? (nowUs - leftEncoder.lastReadUs) / 1000 : 9999;
  const uint32_t rightAgeMs = rightEncoder.lastReadUs ? (nowUs - rightEncoder.lastReadUs) / 1000 : 9999;
  const uint32_t imuAgeMs = (nowUs - lastImuSampleUs) / 1000;
  char frame[384];
  int length = snprintf(frame, sizeof(frame),
      "{\"talosPose\":1,\"boot\":%lu,\"seq\":%lu,\"us\":%lu,\"a\":[%.2f,%.2f,%.2f,%.2f],\"e\":[%u,%u,%u,%u,%lu,%lu],\"c\":[%d,%.3f,%.3f,%d,%d],\"m\":[%d,%d,%d,%d],\"h\":[%lu,%d,%d,%d,%d],\"hz\":%.1f,\"p\":%.1f}\n",
      (unsigned long)poseStreamBootId, (unsigned long)++poseStreamSequence, (unsigned long)nowUs,
      angleDeg, rollDeg, gyroRateDps, rollRateDps,
      leftEncoder.raw, rightEncoder.raw, leftEncoder.magneticStatus, rightEncoder.magneticStatus,
      (unsigned long)leftAgeMs, (unsigned long)rightAgeMs,
      extendedHeightCaptured, leftExtendedDeg, rightExtendedDeg, LEFT_LEG_ENCODER_SIGN, RIGHT_LEG_ENCODER_SIGN,
      leftWheelPulse, rightWheelPulse, leftLegPulse, rightLegPulse,
      (unsigned long)imuAgeMs, imuFaultStreak == 0 && imuAgeMs < 20, armed, standingEnabled, benchModeActive,
      measuredControlHz, pidOutput);
  if (length > 0 && length < (int)sizeof(frame) && Serial.availableForWrite() >= length) {
    Serial.write((const uint8_t *)frame, length);
  }
}

void handleCommand(String command) {
  command.trim();
  if (command.startsWith("wifi-add ")) {
    if (!allActuatorsOff() || pendingLocalWifiAdd) {
      Serial.println("Wi-Fi add refused: stop all motors and wait for the previous request.");
      return;
    }
    JsonDocument request;
    if (deserializeJson(request, command.substring(9))) {
      Serial.println("Use wifi-add JSON with ssid and password. Credentials are not echoed.");
      return;
    }
    const char *ssid = request["ssid"] | "";
    const char *password = request["password"] | "";
    size_t ssidLength = strlen(ssid);
    size_t passwordLength = strlen(password);
    if (ssidLength == 0 || ssidLength > 32 || passwordLength < 8 || passwordLength > 63) {
      Serial.println("Invalid Wi-Fi credential lengths.");
      return;
    }
    strlcpy(pendingLocalWifiSsid, ssid, sizeof(pendingLocalWifiSsid));
    strlcpy(pendingLocalWifiPassword, password, sizeof(pendingLocalWifiPassword));
    pendingLocalWifiAdd = true;
    Serial.println("Local Wi-Fi request queued. Existing profiles are preserved.");
    return;
  }
  command.toLowerCase();
  if (command.isEmpty()) return;
  if (command == "stop" || command == "disarm") {
    takeSerialControl();
    beginEmergencyStop(BalanceStopReason::MANUAL_STOP);
  } else if (command == "help") {
    printHelp();
  } else if (command == "pose-stream on") {
    poseStreamDeadlineMs = millis() + 3000;
    if (!poseStreamEnabled) {
      poseStreamEnabled = true;
      nextPoseStreamUs = micros();
      Serial.println("USB pose stream enabled: read-only, 25 Hz, 3-second heartbeat lease.");
    }
  } else if (command == "pose-stream off") {
    poseStreamEnabled = false;
    Serial.println("USB pose stream disabled. Motor control unchanged.");
  } else if (command == "pose") {
    if (Serial.availableForWrite() >= 700) printPose();
  } else if (command == "wifi-status") {
    TalosWifiManager::StatusSnapshot wifi{};
    talosWifiManager.getStatus(wifi);
    Serial.printf("Wi-Fi state=%s connected=%d ssid=%s ip=%s profiles=%u result=%s\n",
        TalosWifiManager::stateName(wifi.state), wifi.connected, wifi.currentSsid,
        wifi.ipAddress, wifi.profileCount, wifi.lastResult);
  } else if (command == "magnet-status") {
    Serial.printf("AS5600 STATUS left=0x%02X right=0x%02X raw=%u/%u angle=%.2f/%.2f MD=%d/%d weak=%d/%d strong=%d/%d\n",
        leftEncoder.magneticStatus, rightEncoder.magneticStatus, leftEncoder.raw, rightEncoder.raw,
        leftEncoder.angleDeg, rightEncoder.angleDeg, leftEncoder.magnetDetected, rightEncoder.magnetDetected,
        !!(leftEncoder.magneticStatus & 0x10), !!(rightEncoder.magneticStatus & 0x10),
        !!(leftEncoder.magneticStatus & 0x08), !!(rightEncoder.magneticStatus & 0x08));
  } else if (command == "status") {
    if (Serial.availableForWrite() >= 2400) printStatus();
  } else if (command == "local") {
    if (!allActuatorsOff()) {
      Serial.println("Send stop before changing control owner.");
      return;
    }
    takeSerialControl();
    Serial.println("Serial control selected. Dashboard E-stop and fresh OFF still stop all motors.");
  } else if (command == "remote") {
    beginEmergencyStop(BalanceStopReason::MANUAL_STOP);
    serialControlActive = false;
    dashboardHasSeenOff = false;
    enterAutoState(AutoStartupState::WAIT_INITIAL_DELAY);
    Serial.println("Dashboard control selected. Send OFF then ON when supported upright.");
  } else if (command == "telemetry-on") {
    telemetryEnabled = true;
    nextTelemetryMs = millis() + TELEMETRY_INTERVAL_MS;
  } else if (command == "telemetry-off") {
    telemetryEnabled = false;
  } else if (command == "capture-bent" || command == "capture-extended" ||
             command == "stand" || command == "capture-height" || command == "zero") {
    if (!allActuatorsOff()) {
      Serial.println("Capture refused: support the body and send stop first.");
      return;
    }
    takeSerialControl();
    bool success = command == "zero" ? captureBalancePitchOnly() :
        (command == "capture-bent" || command == "capture-extended")
            ? captureHeightEndpoint(command == "capture-bent") : captureDesiredHeightPose();
    if (!success) Serial.println("Capture failed; keep supported and check sensors.");
  } else if (command == "hold" || command == "resume-stand") {
    if (!standPoseCaptured || sensorFault || !leftEncoder.valid || !rightEncoder.valid ||
        (int32_t)(emergencyStopActiveUntilMs - millis()) > 0) {
      Serial.println("Hold refused: capture stand pose, clear faults, and wait after stop.");
      return;
    }
    if (fabsf(wrap180(leftStandTargetDeg - leftEncoder.angleDeg)) > 12.0f ||
        fabsf(wrap180(rightStandTargetDeg - rightEncoder.angleDeg)) > 12.0f) {
      Serial.println("Hold refused: saved pose too far away. Capture current supported pose using stand.");
      return;
    }
    takeSerialControl();
    enableStandingHold();
  } else if (command == "bench-hold") {
    if (!allActuatorsOff() || !leftEncoder.valid || !rightEncoder.valid || sensorFault ||
        (int32_t)(emergencyStopActiveUntilMs - millis()) > 0) {
      Serial.println("Bench hold refused: stop, wait, and check sensors.");
      return;
    }
    takeSerialControl();
    leftStandTargetDeg = leftEncoder.angleDeg;
    rightStandTargetDeg = rightEncoder.angleDeg;
    standPoseCaptured = true;
    enableStandingHold();
    benchModeActive = true;
    leftRequestedJointDeg = leftStandTargetDeg;
    rightRequestedJointDeg = rightStandTargetDeg;
    strlcpy(jointCommandState, "holding", sizeof(jointCommandState));
    Serial.println("Leg-only bench hold enabled; wheels neutral.");
  } else if (command.startsWith("bend ")) {
    takeSerialControl();
    float leftBend, rightBend, speed = 6.0f;
    if (sscanf(command.c_str(), "bend %f %f %f", &leftBend, &rightBend, &speed) < 2 ||
        !requestJointBend(leftBend, rightBend, speed)) {
      Serial.println("Bend refused: bend <left degrees> <right degrees> <1..15 deg/s>.");
    } else Serial.println(jointCommandMessage);
  } else if (command == "legs-off") {
    takeSerialControl();
    beginEmergencyStop(BalanceStopReason::MANUAL_STOP);
  } else if (command == "arm" || command == "arm-saved") {
    if (benchModeActive || !standingEnabled || !automaticStartupStable() ||
        (int32_t)(emergencyStopActiveUntilMs - millis()) > 0 ||
        (int32_t)(millis() - startupCompleteMs) < 0) {
      Serial.println("Arm refused: need healthy sensors, leg hold, upright body and low gyro rates.");
      return;
    }
    takeSerialControl();
    pidIntegral = 0.0f;
    pidOutput = 0.0f;
    wheelManualTestActive = false;
    lastBalanceStopReason = BalanceStopReason::NONE;
    armed = true;
    Serial.println("Armed using SAVED balance zero; no automatic zero capture.");
  } else if (command == "wheel-stop") {
    stopWheelManualTest();
  } else if (command == "wheel-forward" || command == "wheel-reverse" ||
             command == "left-wheel-forward" || command == "left-wheel-reverse" ||
             command == "right-wheel-forward" || command == "right-wheel-reverse") {
    if (!allActuatorsOff() || (int32_t)(emergencyStopActiveUntilMs - millis()) > 0) {
      Serial.println("Wheel check refused: stop first, wait, and raise the wheels.");
      return;
    }
    takeSerialControl();
    int demand = command.endsWith("reverse") ? -100 : 100;
    if (command.startsWith("left-")) startWheelManualTest(demand, 0);
    else if (command.startsWith("right-")) startWheelManualTest(0, demand);
    else startWheelManualTest(demand, demand);
  } else if (command == "imu-reset") {
    if (!allActuatorsOff()) {
      Serial.println("IMU reset refused: support body and stop first.");
      return;
    }
    takeSerialControl();
    recoverIMU();
  } else if (command == "assist-on" || command == "assist-off") {
    takeSerialControl();
    legAssistEnabled = command == "assist-on";
  } else if (command == "save") {
    if (!allActuatorsOff()) {
      Serial.println("Save refused while active.");
      return;
    }
    saveCalibration();
    Serial.println(saveRuntimeSettings() ? "Runtime settings saved." : "Runtime settings NOT saved.");
  } else if (command.startsWith("set ")) {
    if (!parseSetCommand(command)) Serial.println("Invalid value or unsafe state.");
  } else {
    Serial.println("Unknown command. Send help.");
  }
}

void serviceSerial() {
  uint8_t bytesHandled = 0;
  while (Serial.available() && bytesHandled++ < 32) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (!serialLine.isEmpty()) { handleCommand(serialLine); serialLine = ""; }
    } else if (serialLine.length() < 256) serialLine += c;
  }
}

// --------------------------- Arduino setup ---------------------------------
void setup() {
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  poseStreamBootId = esp_random();
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  delay(700);
  Serial.println("===============================================");
  Serial.println("TALOS V17 - SMOOTH BALANCE + TARGET-CENTERED LEG HOLD");
  Serial.println("BOOT: motors neutral; fresh supported arming required");
  Serial.println("===============================================");
  // Both sensors support Fast-mode I2C. 400 kHz removes most of the sensor-read
  // latency from the 2 ms wheel-control budget.
  Wire.begin(MAIN_SDA_PIN, MAIN_SCL_PIN, 400000);
  rightEncoderBus.begin(RIGHT_ENCODER_SDA_PIN, RIGHT_ENCODER_SCL_PIN, 400000);
  Wire.setTimeOut(4);
  rightEncoderBus.setTimeOut(4);

  // Servo outputs are attached before automatic startup, but remain neutral.
  ESP32PWM::allocateTimer(0); ESP32PWM::allocateTimer(1); ESP32PWM::allocateTimer(2); ESP32PWM::allocateTimer(3);

  leftWheel.setPeriodHertz(50); rightWheel.setPeriodHertz(50); leftLeg.setPeriodHertz(50); rightLeg.setPeriodHertz(50);
  leftWheel.attach(LEFT_WHEEL_PIN, 1000, 2000); rightWheel.attach(RIGHT_WHEEL_PIN, 1000, 2000);
  leftLeg.attach(LEFT_LEG_PIN, 500, 2500); rightLeg.attach(RIGHT_LEG_PIN, 500, 2500);
  
  neutralWheels(); neutralLegs();
  loadCalibration();
  loadRuntimeSettings();
  loadLastWifiCommandId();
  loadLastPostureCommandId();

  // One startup attempt is made immediately. Any failure is retried forever
  // by the automatic state machine; no terminal command is needed.
  automaticInitializeAndCalibrateMPU();

  readAS5600(Wire, leftEncoder, 0.0f); readAS5600(rightEncoderBus, rightEncoder, 0.0f);
  initializeHeightTargetFromStandPose();
  startupCompleteMs = millis() + 1000;
  lastControlRunUs = micros();
  lastLegControlUs = lastControlRunUs;
  timingWindowUs = lastControlRunUs;
  nextControlUs = lastControlRunUs + CONTROL_PERIOD_US;
  nextLegUs = micros() + LEG_PERIOD_US;
  nextTelemetryMs = millis() + TELEMETRY_INTERVAL_MS;
  autoStateSinceMs = millis();
  autoStableSinceMs = 0;
  autoStartupState = AutoStartupState::WAIT_INITIAL_DELAY;
  xTaskCreatePinnedToCore(firebaseTask, "TALOS-Firebase", 12288, nullptr, 1,
                         &firebaseTaskHandle, 0);
  Serial.println("Booted DISARMED. Existing Wi-Fi, calibration, and cutoff settings retained.");
  Serial.println("Dashboard: OFF then ON, upright and supported. Saved balance zero is not recaptured.");
  Serial.println("Serial: local, stand, hold, arm-saved. Stop neutralizes every servo.");
  Serial.println("No lying-down recovery or obstacle climbing is enabled.");
  Serial.printf("Controller target 500 Hz; servo PWM 50 Hz; KP=%.1f KD=%.2f.\n", KP, KD);
}

// ---------------------------- Main loop -------------------------------------
void loop() {
  serviceAutomaticStartup();
  uint32_t nowUs = micros();
  if ((int32_t)(nowUs - nextControlUs) >= 0) {
    uint32_t latenessUs = nowUs - nextControlUs;
    maximumControlLatenessUs = max(maximumControlLatenessUs, latenessUs);
    if ((armed || standingEnabled || wheelManualTestActive) &&
        (uint32_t)(nowUs - lastControlRunUs) > MAX_CONTROL_GAP_US) {
      beginEmergencyStop(BalanceStopReason::CONTROL_TIMING);
    }
    lastControlRunUs = nowUs;
    if (latenessUs >= CONTROL_PERIOD_US) {
      controlLoopOverruns++;
      nextControlUs = nowUs + CONTROL_PERIOD_US;
    } else nextControlUs += CONTROL_PERIOD_US;
    controlLoopCount++;
    ImuSample sample;
    if (readMPU(sample)) {
      uint32_t sampleUs = micros();
      uint32_t elapsedUs = sampleUs - lastImuSampleUs;
      float dt = constrain(elapsedUs / 1000000.0f, 0.0001f, 0.03f);
      lastImuSampleUs = sampleUs;
      imuFaultStreak = 0;
      accelAngleDeg = atan2f((float)sample.ay, (float)sample.az) * RAD_TO_DEG;
      accelRollDeg = atan2f((float)sample.ax, (float)sample.az) * RAD_TO_DEG;
      gyroRateDps = sample.gx / 65.5f - gyroBiasDps;
      rollRateDps = -(sample.gy / 65.5f - rollRateBiasDps);
      yawRateDps = sample.gz / 65.5f - yawRateBiasDps;
      float accelX = sample.ax / 8192.0f;
      float accelY = sample.ay / 8192.0f;
      float accelZ = sample.az / 8192.0f;
      accelMagG = sqrtf(accelX * accelX + accelY * accelY + accelZ * accelZ);
      float trust = constrain(1.0f - fabsf(accelMagG - 1.0f) / 0.15f, 0.0f, 1.0f);
      float accelWeight = dt / (FILTER_TIME_CONSTANT_S + dt) * trust;
      if (elapsedUs > MAX_CONTROL_GAP_US) {
        angleDeg = accelAngleDeg;
        rollDeg = accelRollDeg;
      } else {
        angleDeg = wrap180(angleDeg + gyroRateDps * dt);
        rollDeg = wrap180(rollDeg + rollRateDps * dt);
        angleDeg = wrap180(angleDeg + accelWeight * wrap180(accelAngleDeg - angleDeg));
        rollDeg = wrap180(rollDeg + accelWeight * wrap180(accelRollDeg - rollDeg));
      }
      predictedAngleDeg = angleDeg + gyroRateDps * BALANCE_PREDICTION_TIME_S;
      updateWheelControl(dt);
      timingWindowSamples++;
    } else {
      imuErrors++;
      if (imuFaultStreak < 65535) imuFaultStreak++;
      neutralWheels();
      if ((uint32_t)(micros() - lastImuSampleUs) >= IMU_STALE_LIMIT_US) {
        sensorFault = true;
        if (armed || standingEnabled || wheelManualTestActive) {
          beginEmergencyStop(BalanceStopReason::IMU_FAULT);
        }
      }
    }
  }
  nowUs = micros();
  if ((int32_t)(nowUs - nextLegUs) >= 0) {
    float legDt = constrain((nowUs - lastLegControlUs) / 1000000.0f, 0.001f, 0.03f);
    lastLegControlUs = nowUs;
    nextLegUs = nowUs + LEG_PERIOD_US;
    updateLegControl(legDt);
  }
  if ((uint32_t)(nowUs - timingWindowUs) >= 1000000) {
    measuredControlHz = timingWindowSamples * 1000000.0f / (nowUs - timingWindowUs);
    timingWindowSamples = 0;
    timingWindowUs = nowUs;
  }
  serviceSerial();
  servicePendingPostureCommand();
  serviceAutomaticStatusLed();
  servicePoseStream();
  if (telemetryEnabled && !poseStreamEnabled && (int32_t)(millis() - nextTelemetryMs) >= 0) {
    nextTelemetryMs = millis() + TELEMETRY_INTERVAL_MS;
    if (Serial.availableForWrite() >= 256) printCompactStatus();
  }
}
