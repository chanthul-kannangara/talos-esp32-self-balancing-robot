#include "TalosWifiManager.h"

#include <Preferences.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdarg>
#include <cstring>

namespace {

constexpr char kNvsNamespace[] = "taloswifi";
constexpr char kProfilesKey[] = "profiles_v1";
constexpr char kPendingKey[] = "pending_v1";
constexpr char kSetupPassword[] = "talossetup";
constexpr char kSeedSsid[] = "S21 Ultra";
constexpr char kSeedPassword[] = "12345678";

const char kPortalHtml[] PROGMEM = R"TALOSPORTAL(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>TALOS Wi-Fi setup</title>
  <style>
    :root{color-scheme:dark;font-family:Inter,system-ui,sans-serif;background:#0b0d12;color:#f5f7fb}
    *{box-sizing:border-box}body{margin:0;padding:24px;background:linear-gradient(145deg,#101522,#080a0f);min-height:100vh}
    main{max-width:720px;margin:auto}.top{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:18px}
    h1{font-size:24px;margin:0}p{color:#aeb7c8;margin:6px 0}.pill{padding:8px 12px;border:1px solid #2d374a;border-radius:999px;color:#6ee7b7}
    section{background:#141923;border:1px solid #252d3b;border-radius:18px;padding:18px;margin:14px 0;box-shadow:0 16px 44px #0005}
    h2{font-size:16px;margin:0 0 14px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
    label{display:grid;gap:6px;font-size:13px;color:#c6cede}input,select,button{font:inherit;border-radius:11px}
    input,select{width:100%;padding:11px 12px;border:1px solid #354057;background:#0e121a;color:#fff}
    button{border:1px solid #3b4b68;background:#1b2639;color:#eef4ff;padding:10px 13px;cursor:pointer}
    button.primary{background:#2f6fed;border-color:#4d85f2}button.danger{color:#ffb4b4;border-color:#723b45;background:#29171b}
    button:disabled{opacity:.5;cursor:wait}.actions{display:flex;gap:9px;flex-wrap:wrap;margin-top:14px}
    .row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:12px;align-items:center;padding:12px 0;border-top:1px solid #252d3b}
    .row:first-child{border-top:0}.ssid{font-weight:700;overflow-wrap:anywhere}.meta{font-size:12px;color:#99a5b8}
    .row-actions{display:flex;gap:6px}.status{padding:11px 12px;border-radius:11px;background:#0e121a;color:#c9d4e7;white-space:pre-wrap}
    .ok{color:#6ee7b7}.bad{color:#fca5a5}@media(max-width:560px){body{padding:14px}.grid{grid-template-columns:1fr}.top{align-items:flex-start;flex-direction:column}}
  </style>
</head>
<body>
<main>
  <div class="top"><div><h1>TALOS Wi-Fi setup</h1><p>Credentials stay between this device and your ESP32.</p></div><span class="pill">192.168.4.1</span></div>
  <section><h2>Robot connection</h2><div id="status" class="status">Loading...</div><div class="actions"><button id="refresh">Refresh</button><button id="scan">Scan nearby</button><button id="reconnect">Reconnect saved</button><button id="exit">Close setup AP</button></div></section>
  <section><h2>Add and test a network</h2>
    <form id="addForm"><div class="grid">
      <label>Network name (SSID)<input id="ssid" name="ssid" maxlength="32" required autocomplete="off"></label>
      <label>Password<input id="password" name="password" type="password" maxlength="63" autocomplete="new-password" placeholder="Leave blank only for an open network"></label>
      <label>Priority<select id="priority" name="priority"><option value="0">1 - highest</option><option value="1">2</option><option value="2">3</option><option value="3">4</option><option value="4">5</option></select></label>
    </div><div class="actions"><button class="primary" type="submit">Save and test</button></div></form>
  </section>
  <section><h2>Saved networks</h2><div id="saved"><p>Loading...</p></div></section>
  <section><h2>Nearby networks</h2><div id="nearby"><p>Run a scan to update this list.</p></div></section>
</main>
<script>
const TOKEN="%TOKEN%";
let saved=[];
const esc=s=>String(s??"");
async function request(url,options={}){
  const headers={...(options.headers||{}),"X-TALOS-Token":TOKEN};
  const response=await fetch(url,{...options,headers,cache:"no-store"});
  const body=await response.json().catch(()=>({ok:false,message:"Invalid device response"}));
  if(!response.ok||body.ok===false)throw new Error(body.message||body.code||"Request failed");
  return body;
}
function button(text,fn,kind=""){const b=document.createElement("button");b.textContent=text;b.className=kind;b.onclick=fn;return b}
function renderSaved(){
  const box=document.querySelector("#saved");box.replaceChildren();
  if(!saved.length){const p=document.createElement("p");p.textContent="No saved networks.";box.append(p);return}
  saved.sort((a,b)=>a.priority-b.priority).forEach((profile,index)=>{
    const row=document.createElement("div");row.className="row";
    const info=document.createElement("div");const name=document.createElement("div");name.className="ssid";name.textContent=profile.ssid+(profile.active?" - connected":"");
    const meta=document.createElement("div");meta.className="meta";meta.textContent=`Priority ${profile.priority+1} - ${profile.verified?"verified":"not yet verified"}`;
    info.append(name,meta);const actions=document.createElement("div");actions.className="row-actions";
    actions.append(button("Up",()=>move(profile.id,-1)),button("Down",()=>move(profile.id,1)),button("Remove",()=>removeProfile(profile.id),"danger"));row.append(info,actions);box.append(row)
  })
}
function renderNearby(items){
  const box=document.querySelector("#nearby");box.replaceChildren();
  if(!items.length){const p=document.createElement("p");p.textContent="No networks found yet.";box.append(p);return}
  items.forEach(network=>{const row=document.createElement("div");row.className="row";const info=document.createElement("div");
    const name=document.createElement("div");name.className="ssid";name.textContent=network.ssid||"Hidden network";
    const meta=document.createElement("div");meta.className="meta";meta.textContent=`${network.rssi} dBm - channel ${network.channel}`;
    info.append(name,meta);row.append(info,button("Use",()=>{document.querySelector("#ssid").value=network.ssid;document.querySelector("#password").focus()}));box.append(row)})
}
async function refresh(){
  try{
    const [state,networks]=await Promise.all([request("/api/status"),request("/api/networks")]);
    const el=document.querySelector("#status");el.className="status "+(state.connected?"ok":"");
    el.textContent=`${state.state}\n${state.connected?state.currentSsid+" - "+state.ipAddress+" - "+state.rssi+" dBm":"Not connected"}\n${state.lastResult||""}`;
    saved=networks.saved||[];renderSaved();renderNearby(networks.scan||[])
  }catch(error){const el=document.querySelector("#status");el.className="status bad";el.textContent=error.message}
}
async function post(url,data={}){return request(url,{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams(data)})}
async function move(id,delta){const ordered=[...saved].sort((a,b)=>a.priority-b.priority);const at=ordered.findIndex(p=>p.id===id),to=at+delta;if(at<0||to<0||to>=ordered.length)return;[ordered[at],ordered[to]]=[ordered[to],ordered[at]];await post("/api/reorder",{ids:ordered.map(p=>p.id).join(",")});await refresh()}
async function removeProfile(id){if(!confirm("Remove this saved network?"))return;try{await post("/api/remove",{id});await refresh()}catch(error){alert(error.message)}}
document.querySelector("#addForm").onsubmit=async event=>{event.preventDefault();const password=document.querySelector("#password");try{await post("/api/profiles",{ssid:document.querySelector("#ssid").value,password:password.value,priority:document.querySelector("#priority").value});password.value="";await refresh()}catch(error){password.value="";alert(error.message)}};
document.querySelector("#refresh").onclick=refresh;
document.querySelector("#scan").onclick=async()=>{try{await post("/api/scan");setTimeout(refresh,2500)}catch(error){alert(error.message)}};
document.querySelector("#reconnect").onclick=async()=>{try{await post("/api/reconnect");setTimeout(refresh,1500)}catch(error){alert(error.message)}};
document.querySelector("#exit").onclick=async()=>{try{await post("/api/exit");document.querySelector("#status").textContent="Setup AP is closing."}catch(error){alert(error.message)}};
refresh();setInterval(refresh,2000);
</script>
</body>
</html>
)TALOSPORTAL";

bool deadlineReached(uint32_t now, uint32_t deadline) {
  return deadline != 0 && static_cast<int32_t>(now - deadline) >= 0;
}

}  // namespace

// C++11 requires storage for a constexpr static member when a library API
// binds it by reference.
constexpr uint16_t TalosWifiManager::kDnsPort;

TalosWifiManager::TalosWifiManager() : webServer_(80) {
  setLastResult("Wi-Fi manager has not started.");
}

bool TalosWifiManager::begin() {
  if (begun_) return true;

  resetStore();
  const bool loaded = loadStore();
  if (store_.count == 0 && !seedDefaultProfile()) {
    setLastResult("Could not create the initial Wi-Fi profile.");
    return false;
  }

  // A pending credential exists only while it is being tested. If power was
  // lost mid-test, discard it and retain the last known-good profile set.
  clearPending();

  WiFi.persistent(false);
  // Keep transient phone-hotspot fades from forcing a full scan. Scans and
  // profile changes remain blocked while balancing, but reassociation to the
  // current AP is safe and happens in the Wi-Fi driver.
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  configurePortalRoutes();
  begun_ = true;
  setLastResult(loaded ? "Looking for saved Wi-Fi networks."
                       : "Initial Wi-Fi profile created.");

  const Result scanResult = startScan(true);
  if (scanResult != Result::OK) {
    buildCandidateOrder();
    startNextSavedCandidate();
  }
  return true;
}

void TalosWifiManager::service() {
  if (!begun_) return;

  servicePortal();
  serviceScan();

  if (state_ == State::CONNECTING ||
      state_ == State::TESTING_NEW_PROFILE) {
    serviceConnectionAttempt();
  }

  const uint32_t now = millis();
  if (state_ == State::CONNECTED && WiFi.status() != WL_CONNECTED) {
    activeStoreIndex_ = -1;
    setLastResult("Wi-Fi connection lost; reassociating with the current network.");
    scheduleReconnect();
  }
  if (WiFi.status() != WL_CONNECTED && activeStoreIndex_ >= 0 &&
      state_ != State::SCANNING && state_ != State::CONNECTING &&
      state_ != State::TESTING_NEW_PROFILE && retryAtMs_ == 0) {
    activeStoreIndex_ = -1;
    setLastResult("Wi-Fi connection lost; failover is waiting.");
    scheduleReconnect();
  }

  // Auto-reconnect does not pass through serviceConnectionAttempt(), so bring
  // the manager state back in sync when the driver restores the station link.
  if (state_ == State::RETRY_WAIT && WiFi.status() == WL_CONNECTED) {
    activeStoreIndex_ = findProfileBySsid(WiFi.SSID().c_str());
    retryAtMs_ = 0;
    state_ = State::CONNECTED;
    setLastResult("Wi-Fi link restored without interrupting local balance.");
  }

  if ((state_ == State::RETRY_WAIT || state_ == State::SETUP_AP) &&
      WiFi.status() != WL_CONNECTED && deadlineReached(now, retryAtMs_) &&
      radioChangesAllowed() && !scanRunning_) {
    retryAtMs_ = 0;
    const Result result = startScan(true);
    if (result != Result::OK) {
      retryAtMs_ = now + kRetryIntervalMs;
    }
  }

  if (setupActive_ && setupCloseAtMs_ != 0 &&
      deadlineReached(now, setupCloseAtMs_) && WiFi.status() == WL_CONNECTED) {
    stopSetupAccessPoint();
  }
}

void TalosWifiManager::end() {
  if (!begun_) return;
  stopSetupAccessPoint();
  WiFi.scanDelete();
  WiFi.disconnect(false, false);
  state_ = State::STOPPED;
  scanRunning_ = false;
  begun_ = false;
  setLastResult("Wi-Fi manager stopped.");
}

void TalosWifiManager::setSafetyGate(SafetyGate gate, void *context) {
  safetyGate_ = gate;
  safetyContext_ = context;
}

TalosWifiManager::Result TalosWifiManager::requestScan() {
  if (!radioChangesAllowed()) return Result::UNSAFE;
  return startScan(false);
}

TalosWifiManager::Result TalosWifiManager::requestReconnect() {
  if (!radioChangesAllowed()) return Result::UNSAFE;
  if (operationBusy()) return Result::BUSY;

  activeStoreIndex_ = -1;
  setupCloseAtMs_ = 0;
  setLastResult("Reconnect requested.");
  return startScan(true);
}

TalosWifiManager::Result TalosWifiManager::requestReconnectToProfile(
    const char *profileId) {
  if (!radioChangesAllowed()) return Result::UNSAFE;
  if (operationBusy()) return Result::BUSY;

  uint32_t id = 0;
  if (!parseProfileId(profileId, id)) return Result::PROFILE_NOT_FOUND;
  const int requestedIndex = findProfileById(id);
  if (requestedIndex < 0) return Result::PROFILE_NOT_FOUND;

  // Try the requested profile first. If it fails, continue through every
  // remaining saved profile in normal priority/RSSI order.
  buildCandidateOrder();
  uint8_t requestedPosition = 0;
  while (requestedPosition < candidateCount_ &&
         candidateOrder_[requestedPosition] != requestedIndex) {
    ++requestedPosition;
  }
  if (requestedPosition >= candidateCount_) return Result::PROFILE_NOT_FOUND;
  const uint8_t selected = candidateOrder_[requestedPosition];
  for (uint8_t i = requestedPosition; i > 0; --i) {
    candidateOrder_[i] = candidateOrder_[i - 1];
  }
  candidateOrder_[0] = selected;
  candidateCursor_ = 1;
  activeStoreIndex_ = -1;
  setupCloseAtMs_ = 0;
  setLastResult("Reconnect to the selected saved network requested.");
  return startStoredConnection(selected) ? Result::OK : Result::RADIO_ERROR;
}

TalosWifiManager::Result TalosWifiManager::requestSetupMode() {
  if (!radioChangesAllowed()) return Result::UNSAFE;
  if (operationBusy()) return Result::BUSY;
  if (!startSetupAccessPoint()) return Result::RADIO_ERROR;
  // A manually requested AP must not remain exposed indefinitely while the
  // normal station connection is healthy. Offline fallback AP mode still has
  // no deadline, so Wi-Fi recovery remains possible without USB.
  if (WiFi.status() == WL_CONNECTED) {
    setupCloseAtMs_ = millis() + kManualSetupTimeoutMs;
  }
  return Result::OK;
}

TalosWifiManager::Result TalosWifiManager::closeSetupMode() {
  if (!setupActive_) return Result::OK;
  if (WiFi.status() != WL_CONNECTED) return Result::RADIO_ERROR;
  stopSetupAccessPoint();
  return Result::OK;
}

TalosWifiManager::Result TalosWifiManager::saveAndTestProfile(
    const char *ssid, const char *password, uint8_t priority) {
  if (!radioChangesAllowed()) return Result::UNSAFE;
  if (operationBusy()) return Result::BUSY;
  if (!ssid) return Result::INVALID_SSID;

  const size_t ssidLength = strnlen(ssid, 33);
  if (ssidLength == 0 || ssidLength > 32) return Result::INVALID_SSID;

  if (!password) password = "";
  const size_t passwordLength = strnlen(password, 65);
  if (passwordLength > 63 ||
      (passwordLength > 0 && passwordLength < 8)) {
    return Result::INVALID_PASSWORD;
  }
  if (priority >= kMaxProfiles) return Result::INVALID_PRIORITY;

  const int existing = findProfileBySsid(ssid);
  if (existing < 0 && store_.count >= kMaxProfiles) {
    return Result::PROFILE_LIMIT;
  }

  memset(&pending_, 0, sizeof(pending_));
  pending_.magic = kPendingMagic;
  pending_.version = kStoreVersion;
  pending_.bytes = sizeof(PendingStore);
  pending_.candidate.id =
      existing >= 0 ? store_.profiles[existing].id : uniqueProfileId(ssid);
  pending_.candidate.priority = priority;
  pending_.candidate.verified = 0;
  strlcpy(pending_.candidate.ssid, ssid,
          sizeof(pending_.candidate.ssid));
  strlcpy(pending_.candidate.password, password,
          sizeof(pending_.candidate.password));

  if (!writePending(pending_.candidate)) {
    memset(&pending_, 0, sizeof(pending_));
    return Result::STORAGE_ERROR;
  }

  pendingActive_ = true;
  if (!setupActive_ && !startSetupAccessPoint()) {
    clearPending();
    return Result::RADIO_ERROR;
  }

  setLastResult("Testing the new network. The password is not sent to cloud services.");
  if (!startPendingConnection()) {
    clearPending();
    return Result::RADIO_ERROR;
  }
  return Result::OK;
}

TalosWifiManager::Result TalosWifiManager::removeProfile(
    const char *profileId) {
  if (!radioChangesAllowed()) return Result::UNSAFE;
  if (operationBusy()) return Result::BUSY;

  uint32_t id = 0;
  if (!parseProfileId(profileId, id)) return Result::PROFILE_NOT_FOUND;
  const int index = findProfileById(id);
  if (index < 0) return Result::PROFILE_NOT_FOUND;
  if (store_.count <= 1) return Result::LAST_PROFILE;
  if (index == activeStoreIndex_ && WiFi.status() == WL_CONNECTED) {
    return Result::ACTIVE_PROFILE;
  }

  for (uint8_t i = static_cast<uint8_t>(index);
       i + 1 < store_.count; ++i) {
    store_.profiles[i] = store_.profiles[i + 1];
  }
  memset(&store_.profiles[store_.count - 1], 0, sizeof(StoredProfile));
  --store_.count;
  normalizePriorities();
  ++store_.revision;
  if (store_.lastGoodId == id) store_.lastGoodId = 0;
  if (!saveStore()) return Result::STORAGE_ERROR;

  if (activeStoreIndex_ > index) --activeStoreIndex_;
  setLastResult("Saved network removed.");
  return Result::OK;
}

TalosWifiManager::Result TalosWifiManager::reorderProfiles(
    const char *const *orderedProfileIds, size_t count) {
  if (!radioChangesAllowed()) return Result::UNSAFE;
  if (operationBusy()) return Result::BUSY;
  if (!orderedProfileIds || count != store_.count ||
      count > kMaxProfiles) {
    return Result::INVALID_ORDER;
  }

  bool seen[kMaxProfiles] = {};
  for (size_t priority = 0; priority < count; ++priority) {
    uint32_t id = 0;
    if (!parseProfileId(orderedProfileIds[priority], id)) {
      return Result::INVALID_ORDER;
    }
    const int index = findProfileById(id);
    if (index < 0 || seen[index]) return Result::INVALID_ORDER;
    seen[index] = true;
  }

  for (size_t priority = 0; priority < count; ++priority) {
    uint32_t id = 0;
    parseProfileId(orderedProfileIds[priority], id);
    const int index = findProfileById(id);
    store_.profiles[index].priority = static_cast<uint8_t>(priority);
  }
  ++store_.revision;
  if (!saveStore()) return Result::STORAGE_ERROR;
  setLastResult("Wi-Fi priority order saved.");
  return Result::OK;
}

size_t TalosWifiManager::copyProfiles(ProfileInfo *out,
                                      size_t capacity) const {
  if (!out || capacity == 0) return 0;
  const size_t resultCount = std::min(capacity,
                                      static_cast<size_t>(store_.count));
  uint8_t order[kMaxProfiles] = {};
  for (uint8_t i = 0; i < store_.count; ++i) order[i] = i;
  std::sort(order, order + store_.count,
            [this](uint8_t left, uint8_t right) {
              return store_.profiles[left].priority <
                     store_.profiles[right].priority;
            });

  for (size_t i = 0; i < resultCount; ++i) {
    const uint8_t index = order[i];
    memset(&out[i], 0, sizeof(ProfileInfo));
    formatProfileId(store_.profiles[index].id, out[i].id);
    strlcpy(out[i].ssid, store_.profiles[index].ssid,
            sizeof(out[i].ssid));
    out[i].priority = store_.profiles[index].priority;
    out[i].verified = store_.profiles[index].verified != 0;
    out[i].active =
        static_cast<int>(index) == activeStoreIndex_ &&
        WiFi.status() == WL_CONNECTED;
  }
  return resultCount;
}

size_t TalosWifiManager::copyScanResults(ScanInfo *out,
                                         size_t capacity) const {
  if (!out || capacity == 0) return 0;
  const size_t resultCount =
      std::min(capacity, static_cast<size_t>(scanResultCount_));
  for (size_t i = 0; i < resultCount; ++i) {
    memset(&out[i], 0, sizeof(ScanInfo));
    strlcpy(out[i].ssid, scanResults_[i].ssid, sizeof(out[i].ssid));
    out[i].rssi = scanResults_[i].rssi;
    out[i].channel = scanResults_[i].channel;
    out[i].authMode = scanResults_[i].authMode;
  }
  return resultCount;
}

void TalosWifiManager::getStatus(StatusSnapshot &out) const {
  memset(&out, 0, sizeof(out));
  out.state = state_;
  out.connected = WiFi.status() == WL_CONNECTED;
  out.setupActive = setupActive_;
  out.scanRunning = scanRunning_;
  out.profileCount = store_.count;
  out.profilesRevision = store_.revision;
  out.rssi = out.connected ? WiFi.RSSI() : -127;
  if (out.connected) {
    strlcpy(out.currentSsid, WiFi.SSID().c_str(),
            sizeof(out.currentSsid));
    strlcpy(out.ipAddress, WiFi.localIP().toString().c_str(),
            sizeof(out.ipAddress));
  }
  strlcpy(out.setupSsid, setupSsid_, sizeof(out.setupSsid));
  if (setupActive_) {
    strlcpy(out.setupIpAddress, WiFi.softAPIP().toString().c_str(),
            sizeof(out.setupIpAddress));
  }
  if (setupCloseAtMs_ != 0 && !deadlineReached(millis(), setupCloseAtMs_)) {
    out.setupClosesInMs = setupCloseAtMs_ - millis();
  }
  strlcpy(out.lastResult, lastResult_, sizeof(out.lastResult));
}

bool TalosWifiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool TalosWifiManager::isSetupModeActive() const {
  return setupActive_;
}

TalosWifiManager::State TalosWifiManager::state() const {
  return state_;
}

const char *TalosWifiManager::stateName(State state) {
  switch (state) {
    case State::SCANNING: return "scanning";
    case State::CONNECTING: return "connecting";
    case State::CONNECTED: return "connected";
    case State::RETRY_WAIT: return "retry-wait";
    case State::SETUP_AP: return "setup-ap";
    case State::TESTING_NEW_PROFILE: return "testing-new-profile";
    case State::STOPPED:
    default: return "stopped";
  }
}

const char *TalosWifiManager::resultName(Result result) {
  switch (result) {
    case Result::OK: return "ok";
    case Result::BUSY: return "busy";
    case Result::UNSAFE: return "robot-not-safe";
    case Result::INVALID_SSID: return "invalid-ssid";
    case Result::INVALID_PASSWORD: return "invalid-password";
    case Result::INVALID_PRIORITY: return "invalid-priority";
    case Result::PROFILE_LIMIT: return "profile-limit";
    case Result::PROFILE_NOT_FOUND: return "profile-not-found";
    case Result::ACTIVE_PROFILE: return "active-profile";
    case Result::LAST_PROFILE: return "last-profile";
    case Result::INVALID_ORDER: return "invalid-order";
    case Result::STORAGE_ERROR: return "storage-error";
    case Result::RADIO_ERROR: return "radio-error";
    default: return "unknown-error";
  }
}

bool TalosWifiManager::loadStore() {
  Preferences preferences;
  if (!preferences.begin(kNvsNamespace, true)) return false;
  const size_t storedLength = preferences.getBytesLength(kProfilesKey);
  ProfileStore loaded{};
  const size_t readLength =
      storedLength == sizeof(loaded)
          ? preferences.getBytes(kProfilesKey, &loaded, sizeof(loaded))
          : 0;
  preferences.end();

  if (readLength != sizeof(loaded) ||
      loaded.magic != kStoreMagic ||
      loaded.version != kStoreVersion ||
      loaded.bytes != sizeof(ProfileStore) ||
      loaded.count > kMaxProfiles ||
      loaded.crc != crc32(&loaded, offsetof(ProfileStore, crc))) {
    resetStore();
    return false;
  }

  for (uint8_t i = 0; i < loaded.count; ++i) {
    const size_t ssidLength = strnlen(loaded.profiles[i].ssid, 33);
    const size_t passwordLength = strnlen(loaded.profiles[i].password, 65);
    if (loaded.profiles[i].id == 0 || ssidLength == 0 ||
        ssidLength > 32 || passwordLength > 63 ||
        (passwordLength > 0 && passwordLength < 8)) {
      resetStore();
      return false;
    }
  }

  store_ = loaded;
  normalizePriorities();
  return true;
}

bool TalosWifiManager::saveStore() {
  store_.magic = kStoreMagic;
  store_.version = kStoreVersion;
  store_.bytes = sizeof(ProfileStore);
  store_.crc = crc32(&store_, offsetof(ProfileStore, crc));

  Preferences preferences;
  if (!preferences.begin(kNvsNamespace, false)) return false;
  const size_t written =
      preferences.putBytes(kProfilesKey, &store_, sizeof(store_));
  preferences.end();
  return written == sizeof(store_);
}

bool TalosWifiManager::writePending(const StoredProfile &profile) {
  pending_.magic = kPendingMagic;
  pending_.version = kStoreVersion;
  pending_.bytes = sizeof(PendingStore);
  pending_.candidate = profile;
  pending_.crc = crc32(&pending_, offsetof(PendingStore, crc));

  Preferences preferences;
  if (!preferences.begin(kNvsNamespace, false)) return false;
  const size_t written =
      preferences.putBytes(kPendingKey, &pending_, sizeof(pending_));
  preferences.end();
  return written == sizeof(pending_);
}

bool TalosWifiManager::clearPending() {
  Preferences preferences;
  bool removed = true;
  if (preferences.begin(kNvsNamespace, false)) {
    if (preferences.isKey(kPendingKey)) removed = preferences.remove(kPendingKey);
    preferences.end();
  } else {
    removed = false;
  }
  memset(&pending_, 0, sizeof(pending_));
  pendingActive_ = false;
  connectingPending_ = false;
  return removed;
}

void TalosWifiManager::resetStore() {
  memset(&store_, 0, sizeof(store_));
  store_.magic = kStoreMagic;
  store_.version = kStoreVersion;
  store_.bytes = sizeof(ProfileStore);
  store_.revision = 1;
}

bool TalosWifiManager::seedDefaultProfile() {
  if (store_.count != 0) return true;
  StoredProfile &profile = store_.profiles[0];
  memset(&profile, 0, sizeof(profile));
  profile.id = uniqueProfileId(kSeedSsid);
  profile.priority = 0;
  profile.verified = 0;
  strlcpy(profile.ssid, kSeedSsid, sizeof(profile.ssid));
  strlcpy(profile.password, kSeedPassword, sizeof(profile.password));
  store_.count = 1;
  ++store_.revision;
  return saveStore();
}

uint32_t TalosWifiManager::crc32(const void *data, size_t length) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = -(crc & 1UL);
      crc = (crc >> 1) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

uint32_t TalosWifiManager::profileHash(const char *ssid) {
  uint32_t hash = 2166136261UL;
  if (!ssid) return hash;
  while (*ssid) {
    hash ^= static_cast<uint8_t>(*ssid++);
    hash *= 16777619UL;
  }
  return hash == 0 ? 1 : hash;
}

uint32_t TalosWifiManager::uniqueProfileId(const char *ssid) const {
  uint32_t id = profileHash(ssid);
  uint32_t salt = 0x9E3779B9UL;
  while (true) {
    const int existing = findProfileById(id);
    if (existing < 0 ||
        strcmp(store_.profiles[existing].ssid, ssid) == 0) {
      return id;
    }
    id ^= salt;
    salt = salt * 1664525UL + 1013904223UL;
    if (id == 0) id = 1;
  }
}

int TalosWifiManager::findProfileById(uint32_t id) const {
  for (uint8_t i = 0; i < store_.count; ++i) {
    if (store_.profiles[i].id == id) return i;
  }
  return -1;
}

int TalosWifiManager::findProfileBySsid(const char *ssid) const {
  if (!ssid) return -1;
  for (uint8_t i = 0; i < store_.count; ++i) {
    if (strcmp(store_.profiles[i].ssid, ssid) == 0) return i;
  }
  return -1;
}

bool TalosWifiManager::parseProfileId(const char *text, uint32_t &id) {
  if (!text || strlen(text) != 8) return false;
  char *end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 16);
  if (!end || *end != '\0' || parsed == 0) return false;
  id = static_cast<uint32_t>(parsed);
  return true;
}

void TalosWifiManager::formatProfileId(uint32_t id, char out[9]) {
  snprintf(out, 9, "%08lX", static_cast<unsigned long>(id));
}

void TalosWifiManager::normalizePriorities() {
  uint8_t order[kMaxProfiles] = {};
  for (uint8_t i = 0; i < store_.count; ++i) order[i] = i;
  std::sort(order, order + store_.count,
            [this](uint8_t left, uint8_t right) {
              if (store_.profiles[left].priority !=
                  store_.profiles[right].priority) {
                return store_.profiles[left].priority <
                       store_.profiles[right].priority;
              }
              return left < right;
            });
  for (uint8_t priority = 0; priority < store_.count; ++priority) {
    store_.profiles[order[priority]].priority = priority;
  }
}

bool TalosWifiManager::commitPendingProfile() {
  if (!pendingActive_) return false;
  const StoredProfile candidate = pending_.candidate;
  int index = findProfileBySsid(candidate.ssid);
  const bool isNew = index < 0;
  if (isNew) {
    if (store_.count >= kMaxProfiles) return false;
    index = store_.count++;
  }

  // Remove the existing profile from its old priority position, insert the
  // candidate at the requested priority, and keep priorities unique.
  uint8_t targetPriority =
      std::min<uint8_t>(candidate.priority, store_.count - 1);
  const uint8_t oldPriority =
      isNew ? static_cast<uint8_t>(store_.count - 1)
            : store_.profiles[index].priority;
  for (uint8_t i = 0; i < store_.count; ++i) {
    if (i == index) continue;
    uint8_t &priority = store_.profiles[i].priority;
    if (!isNew && priority > oldPriority) --priority;
  }
  for (uint8_t i = 0; i < store_.count; ++i) {
    if (i == index) continue;
    uint8_t &priority = store_.profiles[i].priority;
    if (priority >= targetPriority) ++priority;
  }

  store_.profiles[index] = candidate;
  store_.profiles[index].priority = targetPriority;
  store_.profiles[index].verified = 1;
  normalizePriorities();
  store_.lastGoodId = candidate.id;
  ++store_.revision;
  if (!saveStore()) return false;

  activeStoreIndex_ = findProfileById(candidate.id);
  return true;
}

bool TalosWifiManager::radioChangesAllowed() const {
  return safetyGate_ == nullptr || safetyGate_(safetyContext_);
}

bool TalosWifiManager::operationBusy() const {
  return connectingPending_ ||
         state_ == State::CONNECTING ||
         state_ == State::TESTING_NEW_PROFILE ||
         scanRunning_;
}

void TalosWifiManager::setLastResult(const char *format, ...) {
  if (!format) {
    lastResult_[0] = '\0';
    return;
  }
  va_list args;
  va_start(args, format);
  vsnprintf(lastResult_, sizeof(lastResult_), format, args);
  va_end(args);
}

TalosWifiManager::Result TalosWifiManager::startScan(bool forConnection) {
  if (scanRunning_ || state_ == State::CONNECTING ||
      state_ == State::TESTING_NEW_PROFILE) {
    return Result::BUSY;
  }
  if (!radioChangesAllowed()) return Result::UNSAFE;

  WiFi.scanDelete();
  // Arduino-ESP32 derives its whole-scan timeout as max_ms_per_chan * 20.
  // 120 ms timed out on a real connected STA before all 2.4 GHz channels
  // completed. Keep the scan asynchronous, but use the library's reliable
  // 300 ms/channel default; the safety gate allows scans only at Safe Off.
  const int16_t started = WiFi.scanNetworks(true, true, false, 300);
  if (started == WIFI_SCAN_FAILED) return Result::RADIO_ERROR;

  scanRunning_ = true;
  scanForConnection_ = forConnection;
  state_ = State::SCANNING;
  setLastResult(forConnection ? "Scanning saved Wi-Fi networks."
                              : "Scanning nearby Wi-Fi networks.");
  return Result::OK;
}

void TalosWifiManager::serviceScan() {
  if (!scanRunning_) return;
  const int16_t count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) return;

  scanRunning_ = false;
  if (count >= 0) {
    captureScanResults(count);
  } else {
    scanResultCount_ = 0;
  }
  WiFi.scanDelete();

  const bool connectAfterScan = scanForConnection_;
  scanForConnection_ = false;
  if (connectAfterScan) {
    buildCandidateOrder();
    startNextSavedCandidate();
  } else {
    state_ = WiFi.status() == WL_CONNECTED
                 ? State::CONNECTED
                 : (setupActive_ ? State::SETUP_AP : State::RETRY_WAIT);
    if (WiFi.status() != WL_CONNECTED && retryAtMs_ == 0) {
      retryAtMs_ = millis() + 500;
    }
    setLastResult(count >= 0 ? "Nearby Wi-Fi scan complete."
                             : "Nearby Wi-Fi scan failed.");
  }
}

void TalosWifiManager::captureScanResults(int16_t count) {
  scanResultCount_ = 0;
  for (int16_t i = 0;
       i < count && scanResultCount_ < kMaxScanResults; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0 || ssid.length() > 32) continue;
    const int32_t rssi = WiFi.RSSI(i);

    int existing = -1;
    for (uint8_t j = 0; j < scanResultCount_; ++j) {
      if (strcmp(scanResults_[j].ssid, ssid.c_str()) == 0) {
        existing = j;
        break;
      }
    }
    if (existing >= 0 && scanResults_[existing].rssi >= rssi) continue;

    const uint8_t destination =
        existing >= 0 ? static_cast<uint8_t>(existing) : scanResultCount_++;
    memset(&scanResults_[destination], 0, sizeof(InternalScanInfo));
    strlcpy(scanResults_[destination].ssid, ssid.c_str(),
            sizeof(scanResults_[destination].ssid));
    scanResults_[destination].rssi = rssi;
    scanResults_[destination].channel = WiFi.channel(i);
    scanResults_[destination].authMode =
        static_cast<uint8_t>(WiFi.encryptionType(i));
  }

  std::sort(scanResults_, scanResults_ + scanResultCount_,
            [](const InternalScanInfo &left,
               const InternalScanInfo &right) {
              return left.rssi > right.rssi;
            });
}

int32_t TalosWifiManager::rssiForSsid(const char *ssid) const {
  for (uint8_t i = 0; i < scanResultCount_; ++i) {
    if (strcmp(scanResults_[i].ssid, ssid) == 0) {
      return scanResults_[i].rssi;
    }
  }
  return -1000;
}

void TalosWifiManager::buildCandidateOrder() {
  candidateCount_ = store_.count;
  candidateCursor_ = 0;
  for (uint8_t i = 0; i < store_.count; ++i) candidateOrder_[i] = i;

  // Priority always wins. RSSI is used only as a tie-break, so a strong guest
  // network cannot silently override a deliberately higher-priority network.
  std::sort(candidateOrder_, candidateOrder_ + candidateCount_,
            [this](uint8_t left, uint8_t right) {
              const StoredProfile &a = store_.profiles[left];
              const StoredProfile &b = store_.profiles[right];
              if (a.priority != b.priority) return a.priority < b.priority;
              const int32_t aRssi = rssiForSsid(a.ssid);
              const int32_t bRssi = rssiForSsid(b.ssid);
              if (aRssi != bRssi) return aRssi > bRssi;
              return a.id == store_.lastGoodId &&
                     b.id != store_.lastGoodId;
            });
}

void TalosWifiManager::startNextSavedCandidate() {
  if (!radioChangesAllowed()) {
    scheduleReconnect();
    return;
  }

  if (candidateCursor_ >= candidateCount_) {
    setLastResult("No saved network connected. TALOS setup access point is available.");
    const bool setupStarted = startSetupAccessPoint();
    retryAtMs_ = millis() + kRetryIntervalMs;
    state_ = setupStarted ? State::SETUP_AP : State::RETRY_WAIT;
    return;
  }

  const uint8_t index = candidateOrder_[candidateCursor_++];
  if (!startStoredConnection(index)) startNextSavedCandidate();
}

bool TalosWifiManager::startStoredConnection(uint8_t storeIndex) {
  if (storeIndex >= store_.count) return false;
  if (setupActive_) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }
  WiFi.disconnect(false, false);
  WiFi.begin(store_.profiles[storeIndex].ssid,
             store_.profiles[storeIndex].password);
  connectingStoreIndex_ = storeIndex;
  connectingPending_ = false;
  connectDeadlineMs_ = millis() + kConnectTimeoutMs;
  state_ = State::CONNECTING;
  setLastResult("Trying saved network at priority %u.",
                static_cast<unsigned>(store_.profiles[storeIndex].priority + 1));
  return true;
}

bool TalosWifiManager::startPendingConnection() {
  if (!pendingActive_) return false;
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false, false);
  WiFi.begin(pending_.candidate.ssid, pending_.candidate.password);
  connectingStoreIndex_ = -1;
  connectingPending_ = true;
  connectDeadlineMs_ = millis() + kConnectTimeoutMs;
  state_ = State::TESTING_NEW_PROFILE;
  return true;
}

void TalosWifiManager::serviceConnectionAttempt() {
  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    const char *expected =
        connectingPending_
            ? pending_.candidate.ssid
            : (connectingStoreIndex_ >= 0
                   ? store_.profiles[connectingStoreIndex_].ssid
                   : "");
    if (strcmp(WiFi.SSID().c_str(), expected) == 0) {
      handleConnectionSuccess();
      return;
    }
  }

  if (deadlineReached(now, connectDeadlineMs_)) {
    handleConnectionFailure();
  }
}

void TalosWifiManager::handleConnectionSuccess() {
  connectDeadlineMs_ = 0;
  if (connectingPending_) {
    char connectedSsid[33];
    strlcpy(connectedSsid, pending_.candidate.ssid,
            sizeof(connectedSsid));
    if (!commitPendingProfile()) {
      setLastResult("Connected, but saving the profile failed.");
      clearPending();
      loadStore();
      activeStoreIndex_ = -1;
      scheduleReconnect();
      return;
    }
    clearPending();
    setLastResult("New network connected and saved: %s", connectedSsid);
    setupCloseAtMs_ = millis() + kSetupResultHoldMs;
  } else if (connectingStoreIndex_ >= 0) {
    activeStoreIndex_ = connectingStoreIndex_;
    bool storeChanged =
        store_.lastGoodId != store_.profiles[activeStoreIndex_].id ||
        store_.profiles[activeStoreIndex_].verified == 0;
    store_.lastGoodId = store_.profiles[activeStoreIndex_].id;
    store_.profiles[activeStoreIndex_].verified = 1;
    if (storeChanged) {
      ++store_.revision;
      saveStore();
    }
    setLastResult("Connected to saved network at priority %u.",
                  static_cast<unsigned>(
                      store_.profiles[activeStoreIndex_].priority + 1));
    if (setupActive_) setupCloseAtMs_ = millis() + kSetupResultHoldMs;
  }

  connectingPending_ = false;
  connectingStoreIndex_ = -1;
  retryAtMs_ = 0;
  state_ = State::CONNECTED;
}

void TalosWifiManager::handleConnectionFailure() {
  connectDeadlineMs_ = 0;
  WiFi.disconnect(false, false);
  if (connectingPending_) {
    clearPending();
    setLastResult("New network could not connect; it was not saved. Check the SSID and password.");
    buildCandidateOrder();
    startNextSavedCandidate();
    return;
  }

  connectingStoreIndex_ = -1;
  startNextSavedCandidate();
}

void TalosWifiManager::scheduleReconnect() {
  state_ = setupActive_ ? State::SETUP_AP : State::RETRY_WAIT;
  retryAtMs_ = millis() + 500;
}

bool TalosWifiManager::startSetupAccessPoint() {
  if (setupActive_) {
    state_ = State::SETUP_AP;
    return true;
  }
  if (!radioChangesAllowed()) return false;

  const uint16_t macSuffix =
      static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFFULL);
  snprintf(setupSsid_, sizeof(setupSsid_), "TALOS-SETUP-%04X",
           macSuffix);
  portalToken_ = esp_random();
  if (portalToken_ == 0) portalToken_ = 1;

  WiFi.mode(WIFI_AP_STA);
  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);
  if (!WiFi.softAPConfig(apIp, gateway, subnet) ||
      !WiFi.softAP(setupSsid_, kSetupPassword, 1, false, 4)) {
    setLastResult("Could not start the TALOS setup access point.");
    return false;
  }

  dnsServer_.start(kDnsPort, "*", apIp);
  webServer_.begin();
  setupActive_ = true;
  setupCloseAtMs_ = 0;
  state_ = State::SETUP_AP;
  setLastResult("Join %s and open http://192.168.4.1.", setupSsid_);
  return true;
}

void TalosWifiManager::stopSetupAccessPoint() {
  if (!setupActive_) return;
  dnsServer_.stop();
  webServer_.stop();
  WiFi.softAPdisconnect(true);
  setupActive_ = false;
  setupCloseAtMs_ = 0;
  setupSsid_[0] = '\0';
  WiFi.mode(WIFI_STA);
  state_ = WiFi.status() == WL_CONNECTED
               ? State::CONNECTED
               : State::RETRY_WAIT;
  if (state_ == State::RETRY_WAIT) retryAtMs_ = millis() + 500;
}

void TalosWifiManager::configurePortalRoutes() {
  if (portalRoutesConfigured_) return;
  const char *headers[] = {"X-TALOS-Token"};
  webServer_.collectHeaders(headers, 1);

  webServer_.on("/", HTTP_GET, [this]() { handlePortalRoot(); });
  webServer_.on("/api/status", HTTP_GET,
                [this]() { handlePortalStatus(); });
  webServer_.on("/api/networks", HTTP_GET,
                [this]() { handlePortalNetworks(); });
  webServer_.on("/api/scan", HTTP_POST,
                [this]() { handlePortalScan(); });
  webServer_.on("/api/profiles", HTTP_POST,
                [this]() { handlePortalSave(); });
  webServer_.on("/api/remove", HTTP_POST,
                [this]() { handlePortalRemove(); });
  webServer_.on("/api/reorder", HTTP_POST,
                [this]() { handlePortalReorder(); });
  webServer_.on("/api/reconnect", HTTP_POST,
                [this]() { handlePortalReconnect(); });
  webServer_.on("/api/exit", HTTP_POST,
                [this]() { handlePortalExit(); });
  webServer_.on("/generate_204", HTTP_GET,
                [this]() { handlePortalRoot(); });
  webServer_.on("/hotspot-detect.html", HTTP_GET,
                [this]() { handlePortalRoot(); });
  webServer_.on("/ncsi.txt", HTTP_GET,
                [this]() { handlePortalRoot(); });
  webServer_.onNotFound([this]() { handlePortalNotFound(); });
  portalRoutesConfigured_ = true;
}

void TalosWifiManager::servicePortal() {
  if (!setupActive_) return;
  dnsServer_.processNextRequest();
  webServer_.handleClient();
}

bool TalosWifiManager::isPortalClient() {
  if (!setupActive_) return false;
  const IPAddress remote = webServer_.client().remoteIP();
  const IPAddress ap = WiFi.softAPIP();
  return remote[0] == ap[0] && remote[1] == ap[1] &&
         remote[2] == ap[2] && remote[3] != 0;
}

bool TalosWifiManager::authorizePortalMutation() {
  if (!isPortalClient()) {
    sendJson(403, "{\"ok\":false,\"code\":\"ap-client-required\",\"message\":\"Connect through the TALOS setup access point.\"}");
    return false;
  }
  char expected[9];
  snprintf(expected, sizeof(expected), "%08lX",
           static_cast<unsigned long>(portalToken_));
  if (!webServer_.hasHeader("X-TALOS-Token") ||
      !webServer_.header("X-TALOS-Token").equalsIgnoreCase(expected)) {
    sendJson(403, "{\"ok\":false,\"code\":\"bad-session\",\"message\":\"Reload the setup page and try again.\"}");
    return false;
  }
  return true;
}

String TalosWifiManager::jsonEscape(const char *value) const {
  String escaped;
  if (!value) return escaped;
  escaped.reserve(strlen(value) + 8);
  while (*value) {
    const char c = *value++;
    switch (c) {
      case '"': escaped += F("\\\""); break;
      case '\\': escaped += F("\\\\"); break;
      case '\b': escaped += F("\\b"); break;
      case '\f': escaped += F("\\f"); break;
      case '\n': escaped += F("\\n"); break;
      case '\r': escaped += F("\\r"); break;
      case '\t': escaped += F("\\t"); break;
      default:
        if (static_cast<uint8_t>(c) >= 0x20) escaped += c;
        break;
    }
  }
  return escaped;
}

String TalosWifiManager::profilesJson() const {
  ProfileInfo profiles[kMaxProfiles];
  const size_t count = copyProfiles(profiles, kMaxProfiles);
  String json("[");
  json.reserve(768);
  for (size_t i = 0; i < count; ++i) {
    if (i) json += ',';
    json += F("{\"id\":\"");
    json += profiles[i].id;
    json += F("\",\"ssid\":\"");
    json += jsonEscape(profiles[i].ssid);
    json += F("\",\"priority\":");
    json += profiles[i].priority;
    json += F(",\"verified\":");
    json += profiles[i].verified ? F("true") : F("false");
    json += F(",\"active\":");
    json += profiles[i].active ? F("true}") : F("false}");
  }
  json += ']';
  return json;
}

String TalosWifiManager::scanJson() const {
  String json("[");
  json.reserve(1400);
  for (uint8_t i = 0; i < scanResultCount_; ++i) {
    if (i) json += ',';
    json += F("{\"ssid\":\"");
    json += jsonEscape(scanResults_[i].ssid);
    json += F("\",\"rssi\":");
    json += scanResults_[i].rssi;
    json += F(",\"channel\":");
    json += scanResults_[i].channel;
    json += F(",\"authMode\":");
    json += scanResults_[i].authMode;
    json += '}';
  }
  json += ']';
  return json;
}

String TalosWifiManager::statusJson() const {
  StatusSnapshot status{};
  getStatus(status);
  String json;
  json.reserve(600);
  json += F("{\"ok\":true,\"state\":\"");
  json += stateName(status.state);
  json += F("\",\"connected\":");
  json += status.connected ? F("true") : F("false");
  json += F(",\"setupActive\":");
  json += status.setupActive ? F("true") : F("false");
  json += F(",\"scanRunning\":");
  json += status.scanRunning ? F("true") : F("false");
  json += F(",\"currentSsid\":\"");
  json += jsonEscape(status.currentSsid);
  json += F("\",\"ipAddress\":\"");
  json += status.ipAddress;
  json += F("\",\"rssi\":");
  json += status.rssi;
  json += F(",\"profileCount\":");
  json += status.profileCount;
  json += F(",\"profilesRevision\":");
  json += status.profilesRevision;
  json += F(",\"setupSsid\":\"");
  json += jsonEscape(status.setupSsid);
  json += F("\",\"setupIpAddress\":\"");
  json += status.setupIpAddress;
  json += F("\",\"setupClosesInMs\":");
  json += status.setupClosesInMs;
  json += F(",\"lastResult\":\"");
  json += jsonEscape(status.lastResult);
  json += F("\"}");
  return json;
}

String TalosWifiManager::portalPage() const {
  String page(FPSTR(kPortalHtml));
  char token[9];
  snprintf(token, sizeof(token), "%08lX",
           static_cast<unsigned long>(portalToken_));
  page.replace("%TOKEN%", token);
  return page;
}

void TalosWifiManager::sendJson(int code, const String &body) {
  webServer_.sendHeader("Cache-Control", "no-store");
  webServer_.send(code, "application/json", body);
}

void TalosWifiManager::sendResult(Result result,
                                  const char *successMessage) {
  String body;
  body.reserve(260);
  body += F("{\"ok\":");
  body += result == Result::OK ? F("true") : F("false");
  body += F(",\"code\":\"");
  body += resultName(result);
  body += F("\",\"message\":\"");
  body += jsonEscape(result == Result::OK
                         ? (successMessage ? successMessage : "Accepted.")
                         : resultName(result));
  body += F("\"}");
  sendJson(result == Result::OK ? 200 : 400, body);
}

void TalosWifiManager::handlePortalRoot() {
  if (!isPortalClient()) {
    webServer_.send(403, "text/plain",
                    "Connect through the TALOS setup access point.");
    return;
  }
  webServer_.sendHeader("Cache-Control", "no-store");
  webServer_.send(200, "text/html; charset=utf-8", portalPage());
}

void TalosWifiManager::handlePortalStatus() {
  if (!isPortalClient()) {
    sendJson(403, "{\"ok\":false,\"code\":\"ap-client-required\"}");
    return;
  }
  sendJson(200, statusJson());
}

void TalosWifiManager::handlePortalNetworks() {
  if (!isPortalClient()) {
    sendJson(403, "{\"ok\":false,\"code\":\"ap-client-required\"}");
    return;
  }
  String body;
  body.reserve(2400);
  body += F("{\"ok\":true,\"saved\":");
  body += profilesJson();
  body += F(",\"scan\":");
  body += scanJson();
  body += '}';
  sendJson(200, body);
}

void TalosWifiManager::handlePortalScan() {
  if (!authorizePortalMutation()) return;
  sendResult(requestScan(), "Wi-Fi scan started.");
}

void TalosWifiManager::handlePortalSave() {
  if (!authorizePortalMutation()) return;
  String ssid = webServer_.arg("ssid");
  String password = webServer_.arg("password");
  const int priority = webServer_.arg("priority").toInt();
  const Result result =
      priority < 0 || priority >= static_cast<int>(kMaxProfiles)
          ? Result::INVALID_PRIORITY
          : saveAndTestProfile(ssid.c_str(), password.c_str(),
                               static_cast<uint8_t>(priority));
  // Best-effort removal of extra local copies after the NVS staging write.
  password = "";
  sendResult(result, "Network saved temporarily and connection test started.");
}

void TalosWifiManager::handlePortalRemove() {
  if (!authorizePortalMutation()) return;
  const String id = webServer_.arg("id");
  sendResult(removeProfile(id.c_str()), "Saved network removed.");
}

void TalosWifiManager::handlePortalReorder() {
  if (!authorizePortalMutation()) return;
  String ids = webServer_.arg("ids");
  char buffer[kMaxProfiles][9] = {};
  const char *ordered[kMaxProfiles] = {};
  size_t count = 0;
  int start = 0;
  while (start <= static_cast<int>(ids.length()) &&
         count < kMaxProfiles) {
    int comma = ids.indexOf(',', start);
    if (comma < 0) comma = ids.length();
    const String item = ids.substring(start, comma);
    if (item.length() != 8) {
      sendResult(Result::INVALID_ORDER);
      return;
    }
    strlcpy(buffer[count], item.c_str(), sizeof(buffer[count]));
    ordered[count] = buffer[count];
    ++count;
    start = comma + 1;
    if (comma == static_cast<int>(ids.length())) break;
  }
  sendResult(reorderProfiles(ordered, count),
             "Wi-Fi priority order saved.");
}

void TalosWifiManager::handlePortalReconnect() {
  if (!authorizePortalMutation()) return;
  sendResult(requestReconnect(), "Reconnect started.");
}

void TalosWifiManager::handlePortalExit() {
  if (!authorizePortalMutation()) return;
  if (WiFi.status() != WL_CONNECTED) {
    sendResult(Result::RADIO_ERROR);
    return;
  }
  sendResult(Result::OK, "Setup access point will close.");
  setupCloseAtMs_ = millis() + 750;
}

void TalosWifiManager::handlePortalNotFound() {
  if (!isPortalClient()) {
    webServer_.send(404, "text/plain", "Not found");
    return;
  }
  if (webServer_.uri().startsWith("/api/")) {
    sendJson(404, "{\"ok\":false,\"code\":\"not-found\"}");
    return;
  }
  webServer_.sendHeader("Location", "http://192.168.4.1/", true);
  webServer_.send(302, "text/plain", "");
}
