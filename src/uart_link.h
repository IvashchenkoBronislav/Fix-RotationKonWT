#pragma once

#include <Arduino.h>

void uartLinkInit();
void uartLinkUpdate();

void uartSendPing();
void uartRequestAngle();
void uartSendGoto(int angle);
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
