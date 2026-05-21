#include "network_api.h"

#include <ETH.h>
#include <HTTPClient.h>

#include "config.h"
#include "uart_link.h"

namespace {
bool ethStarted = false;
bool ethConnectedLogged = false;
bool registerComplete = false;
String lastSentPeerIp;
uint32_t lastNetworkAttemptMs = 0;
uint32_t lastRegisterAttemptMs = 0;
uint32_t lastStatusPushMs = 0;
uint32_t lastCommandPollMs = 0;
uint32_t lastRemoteStatusRequestMs = 0;
uint32_t pendingCommandId = 0;
uint32_t pendingCommandSentAtMs = 0;
uint32_t lastPendingAckPostMs = 0;
uint8_t pendingAckCode = 0;
String pendingCommandType;
int pendingGotoTargetAngle = -1;
bool pendingControllerAckReceived = false;
bool pendingServerAckReady = false;
String pendingServerAckStatus;
String pendingServerAckMessage;
bool remoteErrorSnapshotReady = false;
uint8_t lastReportedRemoteErrors = 0;
int allowedFromDeg = 0;
int allowedToDeg = 359;
uint32_t lastAllowedFetchMs = 0;

bool sendGetRequest(const String &path, String *responseOut = nullptr, int *statusOut = nullptr);
int extractJsonInt(const String &json, const char *token, int fallbackValue);
String extractJsonString(const String &json, const char *token);

const char *remoteStateToString(uint8_t state) {
  switch (state) {
    case 0:
      return "STOP";
    case 1:
      return "CW";
    case 2:
      return "CCW";
    default:
      return "STOP";
  }
}

int normalizeAngle(int angle) {
  int value = angle % 360;
  if (value < 0) {
    value += 360;
  }
  return value;
}

int angleDelta(int a, int b) {
  int diff = abs(normalizeAngle(a) - normalizeAngle(b));
  if (diff > 180) {
    diff = 360 - diff;
  }
  return diff;
}

bool isAngleAllowed(int angle) {
  const int a = normalizeAngle(angle);
  const int from = normalizeAngle(allowedFromDeg);
  const int to = normalizeAngle(allowedToDeg);

  if (from <= to) {
    return a >= from && a <= to;
  }

  return a >= from || a <= to;
}

uint8_t chooseGotoDirection(int currentDeg, int targetDeg) {
  const int current = normalizeAngle(currentDeg);
  const int target = normalizeAngle(targetDeg);
  const int from = normalizeAngle(allowedFromDeg);
  const int to = normalizeAngle(allowedToDeg);
  const int arcLen = (to - from + 360) % 360;
  const int posCurrent = (current - from + 360) % 360;
  const int posTarget = (target - from + 360) % 360;

  // If we cannot map angles onto the allowed arc, fall back to controller choice.
  if (posCurrent > arcLen || posTarget > arcLen) {
    return 0;
  }

  // Travel strictly within allowed arc: CW means increasing along the arc, CCW means decreasing (wrap via 0/359).
  return posTarget >= posCurrent ? 1 : 2;
}

bool pendingGotoReachedTarget() {
  if (pendingGotoTargetAngle < 0 || !uartHasRemoteStatus()) {
    return false;
  }

  return !uartIsRemoteExecuting() &&
         uartGetRemoteState() == 0 &&
         angleDelta(uartGetRemoteAngle(), pendingGotoTargetAngle) <= 1;
}

String makeApiUrl(const String &path) {
  String url = "http://";
  url += API_HOST;
  url += ":";
  url += String(API_PORT);
  url += path;
  return url;
}

bool fetchAllowedSector() {
  String response;
  if (!sendGetRequest(String("/api/external/devices/") + DEVICE_ID + "/status", &response)) {
    return false;
  }

  const int from = extractJsonInt(response, "\"allowedFromDeg\":", -1);
  const int to = extractJsonInt(response, "\"allowedToDeg\":", -1);
  if (from < 0 || to < 0) {
    return false;
  }

  allowedFromDeg = normalizeAngle(from);
  allowedToDeg = normalizeAngle(to);
  Serial.print("[NET] allowed sector updated from=");
  Serial.print(allowedFromDeg);
  Serial.print(" to=");
  Serial.println(allowedToDeg);
  return true;
}

bool sendJsonRequest(const char *path, const String &payload, String *responseOut = nullptr, int *statusOut = nullptr) {
  HTTPClient http;

  if (!http.begin(makeApiUrl(String(path)))) {
    Serial.println("[NET] HTTP begin failed");
    return false;
  }

  http.setTimeout(API_HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Api-Key", API_KEY);

  const int httpCode = http.POST(payload);
  const String response = http.getString();
  http.end();

  if (responseOut != nullptr) {
    *responseOut = response;
  }

  if (statusOut != nullptr) {
    *statusOut = httpCode;
  }

  Serial.print("[NET] POST ");
  Serial.print(path);
  Serial.print(" code=");
  Serial.println(httpCode);

  if (response.length() > 0) {
    Serial.print("[NET] body=");
    Serial.println(response);
  }

  return httpCode >= 200 && httpCode < 300;
}

bool sendGetRequest(const String &path, String *responseOut, int *statusOut) {
  HTTPClient http;

  if (!http.begin(makeApiUrl(path))) {
    Serial.println("[NET] HTTP begin failed");
    return false;
  }

  http.setTimeout(API_HTTP_TIMEOUT_MS);
  http.addHeader("X-Api-Key", API_KEY);

  const int httpCode = http.GET();
  const String response = http.getString();
  http.end();

  if (responseOut != nullptr) {
    *responseOut = response;
  }

  if (statusOut != nullptr) {
    *statusOut = httpCode;
  }

  Serial.print("[NET] GET ");
  Serial.print(path);
  Serial.print(" code=");
  Serial.println(httpCode);

  if (response.length() > 0) {
    Serial.print("[NET] body=");
    Serial.println(response);
  }

  return httpCode >= 200 && httpCode < 300;
}

int extractJsonInt(const String &json, const char *token, int fallbackValue) {
  const int tokenIndex = json.indexOf(token);
  if (tokenIndex < 0) {
    return fallbackValue;
  }

  int valueIndex = tokenIndex + strlen(token);
  while (valueIndex < json.length() && (json[valueIndex] == ' ' || json[valueIndex] == '\"')) {
    ++valueIndex;
  }

  int endIndex = valueIndex;
  while (endIndex < json.length()) {
    const char ch = json[endIndex];
    if ((ch >= '0' && ch <= '9') || ch == '-') {
      ++endIndex;
      continue;
    }
    break;
  }

  if (endIndex <= valueIndex) {
    return fallbackValue;
  }

  return json.substring(valueIndex, endIndex).toInt();
}

String extractJsonString(const String &json, const char *token) {
  const int tokenIndex = json.indexOf(token);
  if (tokenIndex < 0) {
    return "";
  }

  const int valueIndex = tokenIndex + strlen(token);
  const int endIndex = json.indexOf('"', valueIndex);
  if (endIndex < 0) {
    return "";
  }

  return json.substring(valueIndex, endIndex);
}

bool fetchAndDispatchNextCommand() {
  if (pendingCommandId != 0) {
    return false;
  }

  String response;
  if (!sendGetRequest(String(API_COMMAND_NEXT_PATH) + "?deviceId=" + DEVICE_ID, &response)) {
    return false;
  }

  if (response.indexOf("\"hasCommand\":true") < 0) {
    return false;
  }

  const int commandId = extractJsonInt(response, "\"id\":", 0);
  const String commandType = extractJsonString(response, "\"commandType\":\"");
  if (commandId <= 0 || commandType.length() == 0) {
    Serial.println("[NET] Invalid command payload");
    return false;
  }

  if (commandType == "GOTO") {
    const int angle = extractJsonInt(response, "\"angle\":", -1);
    if (angle < 0) {
      Serial.println("[NET] Missing GOTO angle");
      return false;
    }

    if (!isAngleAllowed(angle)) {
      pendingCommandId = static_cast<uint32_t>(commandId);
      pendingCommandType = commandType;
      pendingServerAckReady = true;
      pendingServerAckStatus = "FAILED";
      pendingServerAckMessage = "Angle outside allowed sector";
      Serial.print("[NET] rejected GOTO outside allowed sector angle=");
      Serial.println(angle);
      return true;
    }

    uint8_t direction = 0;
    if (uartHasRemoteStatus()) {
      direction = chooseGotoDirection(uartGetRemoteAngle(), angle);
    }
    uartSendGoto(angle, direction);
    pendingAckCode = PACKET_GOTO_AZIMUTH;
    pendingGotoTargetAngle = normalizeAngle(angle);
  } else if (commandType == "STOP") {
    uartSendStop();
    pendingAckCode = PACKET_STOP;
    pendingGotoTargetAngle = -1;
  } else {
    Serial.print("[NET] Unsupported command type: ");
    Serial.println(commandType);
    return false;
  }

  pendingCommandId = static_cast<uint32_t>(commandId);
  pendingCommandSentAtMs = millis();
  lastPendingAckPostMs = 0;
  pendingCommandType = commandType;
  pendingControllerAckReceived = false;
  pendingServerAckReady = false;
  pendingServerAckStatus = "";
  pendingServerAckMessage = "";
  Serial.print("[NET] dispatched command id=");
  Serial.print(pendingCommandId);
  Serial.print(" type=");
  Serial.println(pendingCommandType);
  return true;
}

bool sendCommandAck(const char *status, const String &message) {
  if (pendingCommandId == 0) {
    return false;
  }

  String payload = "{";
  payload += "\"deviceId\":\"";
  payload += DEVICE_ID;
  payload += "\",\"commandId\":";
  payload += String(pendingCommandId);
  payload += ",\"status\":\"";
  payload += status;
  payload += "\",\"message\":\"";
  payload += message;
  payload += "\"}";

  const bool ok = sendJsonRequest(API_COMMAND_ACK_PATH, payload);
  if (ok) {
    pendingCommandId = 0;
    pendingCommandSentAtMs = 0;
    lastPendingAckPostMs = 0;
    pendingAckCode = 0;
    pendingCommandType = "";
    pendingGotoTargetAngle = -1;
    pendingControllerAckReceived = false;
    pendingServerAckReady = false;
    pendingServerAckStatus = "";
    pendingServerAckMessage = "";
  }
  return ok;
}

bool flushPendingServerAck(uint32_t now) {
  if (!pendingServerAckReady || pendingCommandId == 0) {
    return false;
  }

  if ((now - lastPendingAckPostMs) < API_COMMAND_ACK_RETRY_MS && lastPendingAckPostMs != 0) {
    return false;
  }

  lastPendingAckPostMs = now;
  if (!sendCommandAck(pendingServerAckStatus.c_str(), pendingServerAckMessage)) {
    Serial.println("[NET] command ACK post failed, will retry");
    return false;
  }

  return true;
}

String buildRegisterPayload() {
  String payload = "{";
  payload += "\"deviceId\":\"";
  payload += DEVICE_ID;
  payload += "\",\"deviceType\":\"";
  payload += DEVICE_TYPE;
  payload += "\",\"firmwareVersion\":\"0.1.0\"";
  payload += ",\"name\":\"";
  payload += DEVICE_DESCRIPTION;
  payload += "\"}";
  return payload;
}

String buildStatusPayload() {
  const uint8_t currentErrors = uartGetRemoteErrors();
  bool errorEvent = false;
  bool errorCleared = false;

  if (!remoteErrorSnapshotReady) {
    errorEvent = currentErrors != 0;
    remoteErrorSnapshotReady = true;
  } else if (currentErrors != lastReportedRemoteErrors) {
    errorEvent = currentErrors != 0;
    errorCleared = currentErrors == 0 && lastReportedRemoteErrors != 0;
  }

  lastReportedRemoteErrors = currentErrors;

  String payload = "{";
  payload += "\"deviceId\":\"";
  payload += DEVICE_ID;
  payload += "\",\"deviceType\":\"";
  payload += DEVICE_TYPE;
  payload += "\",\"firmwareVersion\":\"0.1.0\"";
  payload += ",\"name\":\"";
  payload += DEVICE_DESCRIPTION;
  payload += "\",\"onlineState\":1";
  payload += ",\"motorState\":\"";
  payload += remoteStateToString(uartGetRemoteState());
  payload += "\"";
  payload += ",\"currentAngle\":";
  payload += String(uartGetRemoteAngle());
  payload += ",\"targetAngle\":";
  payload += String(uartGetRemoteTarget());
  payload += ",\"executing\":";
  payload += uartIsRemoteExecuting() ? "1" : "0";
  payload += ",\"errorFlags\":";
  payload += String(currentErrors);
  payload += ",\"errorText\":\"\"";
  payload += ",\"errorEvent\":";
  payload += errorEvent ? "1" : "0";
  payload += ",\"errorCleared\":";
  payload += errorCleared ? "1" : "0";
  payload += "}";
  return payload;
}

bool ethernetReady() {
  if (!ETH.linkUp()) {
    ethConnectedLogged = false;
    return false;
  }

  const IPAddress ip = ETH.localIP();
  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
    ethConnectedLogged = false;
    return false;
  }

  if (!ethConnectedLogged) {
    Serial.print("[NET] Ethernet connected, IP=");
    Serial.println(ip);
    ethConnectedLogged = true;
  }

  return true;
}

void ensureEthernetStarted() {
  if (ethStarted) {
    return;
  }

  const uint32_t now = millis();
  if ((now - lastNetworkAttemptMs) < NETWORK_RETRY_INTERVAL_MS && lastNetworkAttemptMs != 0) {
    return;
  }

  lastNetworkAttemptMs = now;

  Serial.println("[NET] Ethernet start");

  ethStarted = ETH.begin(
      ETH_PHY_ADDR,
      ETH_PHY_POWER,
      ETH_PHY_MDC,
      ETH_PHY_MDIO,
      ETH_PHY_LAN8720,
      ETH_CLOCK_GPIO0_IN);

  if (!ethStarted) {
    Serial.println("[NET] Ethernet start failed");
    return;
  }

  if (USE_STATIC_IP) {
    const bool configOk = ETH.config(
        IPAddress(STATIC_IP[0], STATIC_IP[1], STATIC_IP[2], STATIC_IP[3]),
        IPAddress(STATIC_GATEWAY[0], STATIC_GATEWAY[1], STATIC_GATEWAY[2], STATIC_GATEWAY[3]),
        IPAddress(STATIC_SUBNET[0], STATIC_SUBNET[1], STATIC_SUBNET[2], STATIC_SUBNET[3]),
        IPAddress(STATIC_DNS[0], STATIC_DNS[1], STATIC_DNS[2], STATIC_DNS[3]));

    Serial.print("[NET] Static IP ");
    Serial.println(configOk ? "enabled" : "failed");
  }
}
}

void networkApiInit() {
  ethStarted = false;
  ethConnectedLogged = false;
  registerComplete = false;
  lastSentPeerIp = "";
  lastNetworkAttemptMs = 0;
  lastRegisterAttemptMs = 0;
  lastStatusPushMs = 0;
  lastCommandPollMs = 0;
  pendingCommandId = 0;
  pendingCommandSentAtMs = 0;
  lastPendingAckPostMs = 0;
  pendingAckCode = 0;
  pendingCommandType = "";
  pendingGotoTargetAngle = -1;
  pendingControllerAckReceived = false;
  pendingServerAckReady = false;
  pendingServerAckStatus = "";
  pendingServerAckMessage = "";
  remoteErrorSnapshotReady = false;
  lastReportedRemoteErrors = 0;
  allowedFromDeg = 0;
  allowedToDeg = 359;
  lastAllowedFetchMs = 0;
}

void networkApiUpdate() {
  ensureEthernetStarted();

  if (!ethernetReady()) {
    registerComplete = false;
    lastSentPeerIp = "";
    return;
  }

  const String currentIp = ETH.localIP().toString();
  if (currentIp.length() > 0 && currentIp != lastSentPeerIp) {
    uartSendPeerIp(currentIp.c_str());
    lastSentPeerIp = currentIp;
  }

  const uint32_t now = millis();

  if ((now - lastAllowedFetchMs) >= 10000 || lastAllowedFetchMs == 0) {
    lastAllowedFetchMs = now;
    fetchAllowedSector();
  }

  uint8_t ackCode = 0;
  if (uartConsumeAckCode(&ackCode) && pendingCommandId != 0) {
    if (ackCode == pendingAckCode) {
      pendingControllerAckReceived = true;

      if (pendingCommandType == "STOP") {
        pendingServerAckStatus = "ACK";
        pendingServerAckMessage = "Controller stopped";
        pendingServerAckReady = true;
      } else if (pendingCommandType == "GOTO") {
        Serial.println("[NET] controller accepted GOTO, waiting for target");
      } else {
        pendingServerAckStatus = "ACK";
        pendingServerAckMessage = "Controller ACK for ";
        pendingServerAckMessage += pendingCommandType;
        pendingServerAckReady = true;
      }
    } else {
      Serial.print("[NET] ignored unrelated controller ACK code 0x");
      Serial.println(ackCode, HEX);
    }
  }

  if (pendingCommandId != 0 && !pendingServerAckReady && pendingCommandType == "GOTO" &&
      pendingControllerAckReceived && uartHasRemoteStatus() &&
      uartGetRemoteStatusMs() >= pendingCommandSentAtMs) {
    if (pendingGotoReachedTarget()) {
      pendingServerAckStatus = "ACK";
      pendingServerAckMessage = "Controller reached GOTO target";
      pendingServerAckReady = true;
    }
  }

  if (pendingCommandId != 0 && !pendingServerAckReady && pendingCommandSentAtMs != 0 &&
      (now - pendingCommandSentAtMs) >= API_COMMAND_ACK_TIMEOUT_MS) {
    pendingServerAckStatus = "FAILED";
    if (pendingCommandType == "GOTO" && pendingControllerAckReceived) {
      pendingServerAckMessage = "Controller completion timeout for GOTO";
    } else {
      pendingServerAckMessage = "Controller ACK timeout for ";
      pendingServerAckMessage += pendingCommandType;
    }
    pendingServerAckReady = true;
    Serial.println("[NET] controller command timeout");
  }

  flushPendingServerAck(now);

  if (!registerComplete) {
    if ((now - lastRegisterAttemptMs) >= API_REGISTER_RETRY_MS || lastRegisterAttemptMs == 0) {
      lastRegisterAttemptMs = now;
      registerComplete = sendJsonRequest(API_REGISTER_PATH, buildRegisterPayload());
    }
    return;
  }

  if (!uartHasRemoteStatus()) {
    if ((now - lastRemoteStatusRequestMs) >= UART_REMOTE_STATUS_REQUEST_MS || lastRemoteStatusRequestMs == 0) {
      lastRemoteStatusRequestMs = now;
      uartRequestAngle();
    }
    return;
  }

  if (pendingCommandId != 0 && pendingCommandType == "GOTO" &&
      !pendingServerAckReady &&
      ((now - lastRemoteStatusRequestMs) >= UART_REMOTE_STATUS_REQUEST_MS || lastRemoteStatusRequestMs == 0)) {
    lastRemoteStatusRequestMs = now;
    uartRequestAngle();
  }

  const bool activelyMoving = uartIsRemoteExecuting() || uartGetRemoteState() != 0;
  const uint32_t statusIntervalMs =
      activelyMoving ? API_STATUS_ACTIVE_INTERVAL_MS : API_STATUS_IDLE_INTERVAL_MS;

  if ((now - lastStatusPushMs) >= statusIntervalMs || lastStatusPushMs == 0) {
    lastStatusPushMs = now;
    sendJsonRequest(API_STATUS_PATH, buildStatusPayload());
  }

  if (pendingCommandId != 0) {
    return;
  }

  if ((now - lastCommandPollMs) < API_COMMAND_POLL_INTERVAL_MS && lastCommandPollMs != 0) {
    return;
  }

  lastCommandPollMs = now;
  fetchAndDispatchNextCommand();
}
