#include "serial_cli.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "uart_link.h"

namespace {
char serialBuffer[SERIAL_BUFFER_SIZE];
size_t serialLength = 0;

void handleSerialCommand() {
  serialBuffer[serialLength] = '\0';

  if (strcmp(serialBuffer, "help") == 0) {
    uartPrintHelp();
    return;
  }

  if (strcmp(serialBuffer, "ping") == 0) {
    uartSendPing();
    return;
  }

  if (strcmp(serialBuffer, "angle") == 0) {
    uartRequestAngle();
    return;
  }

  if (strcmp(serialBuffer, "status") == 0) {
    if (uartHasRemoteStatus()) {
      uartPrintRemoteSummary();
    } else {
      Serial.println("[REMOTE] NONE");
    }

    uartPrintLastAck();
    return;
  }

  int value = 0;
  if (sscanf(serialBuffer, "angle %d", &value) == 1) {
    Serial.println("[ERR] Use 'goto <angle>' or plain 'angle'");
    return;
  }

  if (sscanf(serialBuffer, "goto %d", &value) == 1) {
    uartSendGoto(value);
    return;
  }

  Serial.print("[ERR] Unknown command: ");
  Serial.println(serialBuffer);
}
}

void serialCliInit() {
  serialLength = 0;
}

void serialCliUpdate() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());

    if (value == '\r') {
      continue;
    }

    if (value == '\n') {
      if (serialLength > 0) {
        handleSerialCommand();
        serialLength = 0;
      }
      continue;
    }

    if (serialLength < (SERIAL_BUFFER_SIZE - 1)) {
      serialBuffer[serialLength++] = value;
    }
  }
}
