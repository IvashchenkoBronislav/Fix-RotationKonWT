#pragma once

#include <Arduino.h>

constexpr uint32_t DEBUG_BAUD = 115200;
constexpr uint32_t UART2_BAUD = 9600;
constexpr uint32_t NETWORK_RETRY_INTERVAL_MS = 10000;
constexpr uint32_t API_REGISTER_RETRY_MS = 10000;
constexpr uint32_t API_STATUS_IDLE_INTERVAL_MS = 2000;
constexpr uint32_t API_STATUS_ACTIVE_INTERVAL_MS = 250;
constexpr uint32_t API_COMMAND_POLL_INTERVAL_MS = 1000;
constexpr uint32_t API_COMMAND_ACK_TIMEOUT_MS = 30000;
constexpr uint32_t API_COMMAND_ACK_RETRY_MS = 1000;
constexpr uint32_t API_HTTP_TIMEOUT_MS = 5000;
constexpr uint32_t WEB_STATUS_REFRESH_MS = 2000;
constexpr uint32_t UART_REMOTE_STATUS_REQUEST_MS = 1000;

// Backend-facing device identity.
// DEVICE_ID should be unique for each physical unit in the database.
constexpr const char *DEVICE_ID = "r1p-005";
constexpr const char *DEVICE_TYPE = "RotationKonWT";
constexpr const char *DEVICE_DESCRIPTION = "WT32 remote controller";

// Network and API configuration.
constexpr bool USE_STATIC_IP = false;
constexpr uint8_t STATIC_IP[4] = {192, 168, 31, 130};
constexpr uint8_t STATIC_GATEWAY[4] = {192, 168, 31, 1};
constexpr uint8_t STATIC_SUBNET[4] = {255, 255, 255, 0};
constexpr uint8_t STATIC_DNS[4] = {8, 8, 8, 8};

constexpr const char *API_HOST = "161.97.178.105";
constexpr uint16_t API_PORT = 8081;
constexpr const char *API_REGISTER_PATH = "/api/rotation/register";
constexpr const char *API_STATUS_PATH = "/api/rotation/status";
constexpr const char *API_COMMAND_NEXT_PATH = "/api/rotation/commands/next";
constexpr const char *API_COMMAND_ACK_PATH = "/api/rotation/commands/ack";
constexpr const char *API_KEY = "ykR7RN4fftv4onrTQis2_bgsYEQgn2sL5J7-aSvPGKI";

constexpr uint8_t PIN_UART2_RX = 35;
constexpr uint8_t PIN_UART2_TX = 14;

constexpr uint8_t FRAME_SYNC_1 = 0xAA;
constexpr uint8_t FRAME_SYNC_2 = 0x55;

constexpr size_t MAX_PAYLOAD_SIZE = 16;
constexpr size_t SERIAL_BUFFER_SIZE = 48;

enum PacketType : uint8_t {
  PACKET_PING = 0x01,
  PACKET_STATUS = 0x10,
  PACKET_GET_ANGLE = 0x20,
  PACKET_GOTO_AZIMUTH = 0x21,
  PACKET_STOP = 0x22,
  PACKET_PEER_IP = 0x30,
  PACKET_ACK = 0x7F
};

struct Packet {
  uint8_t type;
  uint8_t length;
  uint8_t payload[MAX_PAYLOAD_SIZE];
};
