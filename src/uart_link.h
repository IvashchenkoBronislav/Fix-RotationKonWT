#pragma once

#include <Arduino.h>

void uartLinkInit();
void uartLinkUpdate();

void uartSendPing();
void uartRequestAngle();
// Direction byte is optional and is understood by newer RotationKon firmware:
// 1 = CW, 2 = CCW. Pass 0 to let the controller choose (backward compatible).
void uartSendGoto(int angle, uint8_t direction = 0);
void uartSendStop();
void uartSendPeerIp(const char *ipText);

void uartPrintRemoteSummary();
void uartPrintLastAck();
void uartPrintHelp();

bool uartHasRemoteStatus();
uint32_t uartGetRemoteStatusMs();
uint8_t uartGetRemoteState();
int uartGetRemoteAngle();
uint8_t uartGetRemoteErrors();
bool uartIsRemoteExecuting();
int uartGetRemoteTarget();
const char *uartGetRemoteStateString();
const char *uartGetCommandStateString();
bool uartConsumeAckCode(uint8_t *ackCode);
