#include <Arduino.h>

#include "config.h"
#include "network_api.h"
#include "serial_cli.h"
#include "uart_link.h"
#include "web_ui.h"

void setup() {
  Serial.begin(DEBUG_BAUD);
  delay(300);

  Serial.println();
  Serial.println("WT32 UART2 test");
  Serial.println("Board: WT32-ETH01");
  Serial.print("DEVICE_ID: ");
  Serial.println(DEVICE_ID);
  Serial.print("Device Type: ");
  Serial.println(DEVICE_TYPE);
  Serial.print("Role: ");
  Serial.println(DEVICE_DESCRIPTION);
  Serial.print("UART2 RX=");
  Serial.println(PIN_UART2_RX);
  Serial.print("UART2 TX=");
  Serial.println(PIN_UART2_TX);
  Serial.print("UART2 baud=");
  Serial.println(UART2_BAUD);

  uartLinkInit();
  networkApiInit();
  serialCliInit();
  webUiInit();

  Serial.println("UART2 ready");
  uartPrintHelp();
}

void loop() {
  uartLinkUpdate();
  networkApiUpdate();
  webUiUpdate();
  serialCliUpdate();
}
