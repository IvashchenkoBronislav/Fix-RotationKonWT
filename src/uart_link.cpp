#include "uart_link.h"

#include <HardwareSerial.h>

#include "config.h"

namespace {
HardwareSerial rotorUart(2);

bool remoteStatusValid = false;
uint32_t remoteStatusMs = 0;
uint8_t remoteState = 0;
int remoteAngle = 0;
uint8_t remoteErrors = 0;
bool remoteExecuting = false;
int remoteTarget = 0;

bool ackReceived = false;
uint8_t lastAckCode = 0;
uint32_t lastAckMs = 0;

bool gotoPending = false;
int requestedGotoTarget = 0;

enum RxState : uint8_t {
  RX_WAIT_SYNC_1 = 0,
  RX_WAIT_SYNC_2,
  RX_WAIT_TYPE,
  RX_WAIT_LENGTH,
  RX_WAIT_PAYLOAD,
  RX_WAIT_CHECKSUM
};

RxState rxState = RX_WAIT_SYNC_1;
Packet rxPacket = {};
uint8_t rxPayloadIndex = 0;
uint8_t rxChecksum = 0;

void printHexByte(Stream &stream, uint8_t value) {
  if (value < 0x10) {
    stream.print('0');
  }

  stream.print(value, HEX);
}

uint8_t computeChecksum(const Packet &packet) {
  uint8_t checksum = packet.type ^ packet.length;

  for (uint8_t i = 0; i < packet.length; ++i) {
    checksum ^= packet.payload[i];
  }

  return checksum;
}

void sendPacket(const Packet &packet) {
  rotorUart.write(FRAME_SYNC_1);
  rotorUart.write(FRAME_SYNC_2);
  rotorUart.write(packet.type);
  rotorUart.write(packet.length);

  for (uint8_t i = 0; i < packet.length; ++i) {
    rotorUart.write(packet.payload[i]);
  }

  rotorUart.write(computeChecksum(packet));
}

void printPacket(const char *prefix, const Packet &packet) {
  Serial.print(prefix);
  Serial.print(" type=0x");
  printHexByte(Serial, packet.type);
  Serial.print(" len=");
  Serial.print(packet.length);
  Serial.print(" data=");

  if (packet.length == 0) {
    Serial.println("-");
    return;
  }

  for (uint8_t i = 0; i < packet.length; ++i) {
    if (i > 0) {
      Serial.print(' ');
    }
    printHexByte(Serial, packet.payload[i]);
  }

  Serial.println();
}

const char *stateToString(uint8_t state) {
  switch (state) {
    case 0:
      return "STOP";
    case 1:
      return "CW";
    case 2:
      return "CCW";
    default:
      return "UNKNOWN";
  }
}

const char *commandStateToString() {
  if (!gotoPending) {
    return "IDLE";
  }

  if (remoteExecuting) {
    return "RUNNING";
  }

  if (remoteErrors != 0) {
    return "FAILED";
  }

  if (remoteStatusValid && remoteAngle == requestedGotoTarget) {
    return "DONE";
  }

  return "WAIT";
}

void handlePacket(const Packet &packet) {
  printPacket("[RX2]", packet);

  if (packet.type == PACKET_PING) {
    return;
  }

  if (packet.type == PACKET_STATUS) {
    if (packet.length >= 7) {
      remoteState = packet.payload[0];
      remoteAngle = static_cast<int>(
          static_cast<uint16_t>(packet.payload[1]) |
          (static_cast<uint16_t>(packet.payload[2]) << 8));
      remoteErrors = packet.payload[3];
      remoteExecuting = (packet.payload[4] & 0x01) != 0;
      remoteTarget = static_cast<int>(
          static_cast<uint16_t>(packet.payload[5]) |
          (static_cast<uint16_t>(packet.payload[6]) << 8));

      if (gotoPending && !remoteExecuting) {
        if (remoteErrors == 0 && remoteAngle == requestedGotoTarget) {
          gotoPending = false;
        } else if (remoteErrors != 0) {
          gotoPending = false;
        }
      }

      remoteStatusValid = true;
      remoteStatusMs = millis();
      uartPrintRemoteSummary();
      return;
    }

    if (packet.length >= 4) {
      // Backward-compatible parsing for older controller status frames.
      remoteState = packet.payload[0];
      remoteAngle = static_cast<int>(
          static_cast<uint16_t>(packet.payload[1]) |
          (static_cast<uint16_t>(packet.payload[2]) << 8));
      remoteErrors = packet.payload[3];
      remoteExecuting = false;
      remoteTarget = remoteAngle;

      if (gotoPending && !remoteExecuting) {
        if (remoteErrors == 0 && remoteAngle == requestedGotoTarget) {
          gotoPending = false;
        } else if (remoteErrors != 0) {
          gotoPending = false;
        }
      }

      remoteStatusValid = true;
      remoteStatusMs = millis();
      uartPrintRemoteSummary();
    }

    return;
  }

  if (packet.type == PACKET_GET_ANGLE) {
    return;
  }

  if (packet.type == PACKET_GOTO_AZIMUTH) {
    return;
  }

  if (packet.type == PACKET_STOP) {
    return;
  }

  if (packet.type == PACKET_ACK) {
    if (packet.length > 0) {
      ackReceived = true;
      lastAckCode = packet.payload[0];
      lastAckMs = millis();
      Serial.print("[ACK] 0x");
      printHexByte(Serial, lastAckCode);
      Serial.print(" @");
      Serial.println(lastAckMs);

      if (lastAckCode == PACKET_GOTO_AZIMUTH) {
        gotoPending = true;
      }
    }

    return;
  }
}

void processUartByte(uint8_t value) {
  switch (rxState) {
    case RX_WAIT_SYNC_1:
      if (value == FRAME_SYNC_1) {
        rxState = RX_WAIT_SYNC_2;
      }
      break;

    case RX_WAIT_SYNC_2:
      if (value == FRAME_SYNC_2) {
        rxState = RX_WAIT_TYPE;
      } else {
        rxState = RX_WAIT_SYNC_1;
      }
      break;

    case RX_WAIT_TYPE:
      rxPacket.type = value;
      rxChecksum = value;
      rxState = RX_WAIT_LENGTH;
      break;

    case RX_WAIT_LENGTH:
      rxPacket.length = value;
      rxChecksum ^= value;
      rxPayloadIndex = 0;

      if (rxPacket.length > MAX_PAYLOAD_SIZE) {
        Serial.println("[ERR] UART packet too long");
        rxState = RX_WAIT_SYNC_1;
      } else if (rxPacket.length == 0) {
        rxState = RX_WAIT_CHECKSUM;
      } else {
        rxState = RX_WAIT_PAYLOAD;
      }
      break;

    case RX_WAIT_PAYLOAD:
      rxPacket.payload[rxPayloadIndex++] = value;
      rxChecksum ^= value;

      if (rxPayloadIndex >= rxPacket.length) {
        rxState = RX_WAIT_CHECKSUM;
      }
      break;

    case RX_WAIT_CHECKSUM:
      if (value == rxChecksum) {
        handlePacket(rxPacket);
      } else {
        Serial.print("[ERR] UART checksum mismatch rx=0x");
        printHexByte(Serial, value);
        Serial.print(" calc=0x");
        printHexByte(Serial, rxChecksum);
        Serial.println();
      }

      rxState = RX_WAIT_SYNC_1;
      break;
  }
}
}

void uartLinkInit() {
  rotorUart.begin(UART2_BAUD, SERIAL_8N1, PIN_UART2_RX, PIN_UART2_TX);
}

void uartLinkUpdate() {
  while (rotorUart.available() > 0) {
    processUartByte(static_cast<uint8_t>(rotorUart.read()));
  }
}

void uartSendPing() {
  Packet packet = {};
  packet.type = PACKET_PING;
  packet.length = 0;
  sendPacket(packet);
  printPacket("[TX2]", packet);
}

void uartRequestAngle() {
  Packet packet = {};
  packet.type = PACKET_GET_ANGLE;
  packet.length = 0;
  sendPacket(packet);
  printPacket("[TX2]", packet);
}

void uartSendGoto(int angle, uint8_t direction) {
  Packet packet = {};
  packet.type = PACKET_GOTO_AZIMUTH;
  packet.length = direction == 0 ? 2 : 3;
  packet.payload[0] = static_cast<uint8_t>(angle & 0xFF);
  packet.payload[1] = static_cast<uint8_t>((angle >> 8) & 0xFF);
  if (packet.length >= 3) {
    packet.payload[2] = direction;
  }

  requestedGotoTarget = angle;
  gotoPending = false;

  sendPacket(packet);
  printPacket("[TX2]", packet);
}

void uartSendStop() {
  Packet packet = {};
  packet.type = PACKET_STOP;
  packet.length = 0;

  gotoPending = false;
  requestedGotoTarget = remoteAngle;

  sendPacket(packet);
  printPacket("[TX2]", packet);
}

void uartSendPeerIp(const char *ipText) {
  Packet packet = {};
  packet.type = PACKET_PEER_IP;
  packet.length = 0;

  while (ipText[packet.length] != '\0' && packet.length < MAX_PAYLOAD_SIZE) {
    packet.payload[packet.length] = static_cast<uint8_t>(ipText[packet.length]);
    ++packet.length;
  }

  sendPacket(packet);
  printPacket("[TX2]", packet);
}

void uartPrintRemoteSummary() {
  Serial.println("[REMOTE]");
  Serial.print("cmd=");
  Serial.println(commandStateToString());
  Serial.print("state=");
  Serial.println(stateToString(remoteState));
  Serial.print("angle=");
  Serial.println(remoteAngle);
  Serial.print("target=");
  Serial.println(remoteTarget);
  Serial.print("executing=");
  Serial.println(remoteExecuting ? "YES" : "NO");
  Serial.print("errors=0x");
  printHexByte(Serial, remoteErrors);
  Serial.println();
}

void uartPrintLastAck() {
  Serial.print("lastAck=");

  if (ackReceived) {
    Serial.print("0x");
    printHexByte(Serial, lastAckCode);
    Serial.print(" @");
    Serial.println(lastAckMs);
    return;
  }

  Serial.println("NONE");
}

void uartPrintHelp() {
  Serial.println("Commands:");
  Serial.println("  help");
  Serial.println("  ping");
  Serial.println("  angle           - request current angle");
  Serial.println("  goto <0..359>   - rotate to target angle");
  Serial.println("  status          - print last remote status");
}

bool uartHasRemoteStatus() {
  return remoteStatusValid;
}

uint32_t uartGetRemoteStatusMs() {
  return remoteStatusMs;
}

uint8_t uartGetRemoteState() {
  return remoteState;
}

int uartGetRemoteAngle() {
  return remoteAngle;
}

uint8_t uartGetRemoteErrors() {
  return remoteErrors;
}

bool uartIsRemoteExecuting() {
  return remoteExecuting;
}

int uartGetRemoteTarget() {
  return remoteTarget;
}

const char *uartGetRemoteStateString() {
  return stateToString(remoteState);
}

const char *uartGetCommandStateString() {
  return commandStateToString();
}

bool uartConsumeAckCode(uint8_t *ackCode) {
  if (!ackReceived) {
    return false;
  }

  if (ackCode != nullptr) {
    *ackCode = lastAckCode;
  }

  ackReceived = false;
  return true;
}
