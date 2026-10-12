#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

// TALOS Wi-Fi runs outside the 500 Hz balance loop. Call begin() and service()
// from the Core 0 network task only. The manager never sends Wi-Fi passwords to
// Firebase: credentials are accepted only by the WPA2 local setup portal.
class TalosWifiManager {
 public:
  static constexpr size_t kMaxProfiles = 5;
  static constexpr size_t kMaxScanResults = 16;

  enum class State : uint8_t {
    STOPPED,
    SCANNING,
    CONNECTING,
    CONNECTED,
    RETRY_WAIT,
    SETUP_AP,
    TESTING_NEW_PROFILE,
  };

  enum class Result : uint8_t {
    OK,
    BUSY,
    UNSAFE,
    INVALID_SSID,
    INVALID_PASSWORD,
    INVALID_PRIORITY,
    PROFILE_LIMIT,
    PROFILE_NOT_FOUND,
    ACTIVE_PROFILE,
    LAST_PROFILE,
    INVALID_ORDER,
    STORAGE_ERROR,
    RADIO_ERROR,
  };

  struct ProfileInfo {
    char id[9];
    char ssid[33];
    uint8_t priority;
    bool verified;
    bool active;
  };

  struct ScanInfo {
    char ssid[33];
    int32_t rssi;
    int32_t channel;
    uint8_t authMode;
  };

  struct StatusSnapshot {
    State state;
    bool connected;
    bool setupActive;
    bool scanRunning;
    uint8_t profileCount;
    uint32_t profilesRevision;
    int32_t rssi;
    uint32_t setupClosesInMs;
    char currentSsid[33];
    char ipAddress[16];
    char setupSsid[33];
    char setupIpAddress[16];
    char lastResult[128];
  };

  // Return true only when wheels are neutral, balancing is disarmed, and any
  // smooth leg-release sequence is in a state where radio/NVS work is safe.
  using SafetyGate = bool (*)(void *context);

  TalosWifiManager();

  bool begin();
  void service();
  void end();

  void setSafetyGate(SafetyGate gate, void *context = nullptr);

  Result requestScan();
  Result requestReconnect();
  Result requestReconnectToProfile(const char *profileId);
  Result requestSetupMode();
  Result closeSetupMode();

  // This is intentionally for the local 192.168.4.1 portal, not a cloud API.
  Result saveAndTestProfile(const char *ssid, const char *password,
                            uint8_t priority);
  Result removeProfile(const char *profileId);
  Result reorderProfiles(const char *const *orderedProfileIds, size_t count);

  size_t copyProfiles(ProfileInfo *out, size_t capacity) const;
  size_t copyScanResults(ScanInfo *out, size_t capacity) const;
  void getStatus(StatusSnapshot &out) const;

  bool isConnected() const;
  bool isSetupModeActive() const;
  State state() const;

  static const char *stateName(State state);
  static const char *resultName(Result result);

 private:
  struct StoredProfile {
    uint32_t id;
    uint8_t priority;
    uint8_t verified;
    uint8_t reserved[2];
    char ssid[33];
    char password[65];
  };

  struct ProfileStore {
    uint32_t magic;
    uint16_t version;
    uint16_t bytes;
    uint32_t revision;
    uint32_t lastGoodId;
    uint8_t count;
    uint8_t reserved[3];
    StoredProfile profiles[kMaxProfiles];
    uint32_t crc;
  };

  struct PendingStore {
    uint32_t magic;
    uint16_t version;
    uint16_t bytes;
    StoredProfile candidate;
    uint32_t crc;
  };

  struct InternalScanInfo {
    char ssid[33];
    int32_t rssi;
    int32_t channel;
    uint8_t authMode;
  };

  static constexpr uint32_t kStoreMagic = 0x544C5746UL;    // TLWF
  static constexpr uint32_t kPendingMagic = 0x544C5750UL;  // TLWP
  static constexpr uint16_t kStoreVersion = 1;
  static constexpr uint32_t kConnectTimeoutMs = 9000;
  static constexpr uint32_t kRetryIntervalMs = 30000;
  static constexpr uint32_t kSetupResultHoldMs = 120000;
  static constexpr uint32_t kManualSetupTimeoutMs = 600000;
  static constexpr uint16_t kDnsPort = 53;

  bool loadStore();
  bool saveStore();
  bool writePending(const StoredProfile &profile);
  bool clearPending();
  void resetStore();
  bool seedDefaultProfile();

  static uint32_t crc32(const void *data, size_t length);
  static uint32_t profileHash(const char *ssid);
  uint32_t uniqueProfileId(const char *ssid) const;
  int findProfileById(uint32_t id) const;
  int findProfileBySsid(const char *ssid) const;
  static bool parseProfileId(const char *text, uint32_t &id);
  static void formatProfileId(uint32_t id, char out[9]);
  void normalizePriorities();
  bool commitPendingProfile();

  bool radioChangesAllowed() const;
  bool operationBusy() const;
  void setLastResult(const char *format, ...);

  Result startScan(bool forConnection);
  void serviceScan();
  void captureScanResults(int16_t count);
  int32_t rssiForSsid(const char *ssid) const;
  void buildCandidateOrder();
  void startNextSavedCandidate();
  bool startStoredConnection(uint8_t storeIndex);
  bool startPendingConnection();
  void serviceConnectionAttempt();
  void handleConnectionSuccess();
  void handleConnectionFailure();
  void scheduleReconnect();

  bool startSetupAccessPoint();
  void stopSetupAccessPoint();
  void configurePortalRoutes();
  void servicePortal();
  bool isPortalClient();
  bool authorizePortalMutation();
  String jsonEscape(const char *value) const;
  String profilesJson() const;
  String scanJson() const;
  String statusJson() const;
  String portalPage() const;
  void sendJson(int code, const String &body);
  void sendResult(Result result, const char *successMessage = nullptr);

  void handlePortalRoot();
  void handlePortalStatus();
  void handlePortalNetworks();
  void handlePortalScan();
  void handlePortalSave();
  void handlePortalRemove();
  void handlePortalReorder();
  void handlePortalReconnect();
  void handlePortalExit();
  void handlePortalNotFound();

  ProfileStore store_{};
  PendingStore pending_{};
  bool pendingActive_ = false;

  InternalScanInfo scanResults_[kMaxScanResults]{};
  uint8_t scanResultCount_ = 0;
  uint8_t candidateOrder_[kMaxProfiles]{};
  uint8_t candidateCount_ = 0;
  uint8_t candidateCursor_ = 0;
  int8_t connectingStoreIndex_ = -1;
  int8_t activeStoreIndex_ = -1;

  bool begun_ = false;
  bool scanRunning_ = false;
  bool scanForConnection_ = false;
  bool connectingPending_ = false;
  bool setupActive_ = false;
  bool portalRoutesConfigured_ = false;
  State state_ = State::STOPPED;

  uint32_t connectDeadlineMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint32_t setupCloseAtMs_ = 0;
  uint32_t portalToken_ = 0;
  char setupSsid_[33]{};
  char lastResult_[128]{};

  SafetyGate safetyGate_ = nullptr;
  void *safetyContext_ = nullptr;

  DNSServer dnsServer_;
  WebServer webServer_;
};
