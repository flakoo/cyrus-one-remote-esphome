#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cyrus_ble {

namespace protocol {

inline constexpr char SERVICE_UUID[] = "bc2f4cc6-aaef-4351-9034-d66268e328f0";
inline constexpr char CHARACTERISTIC_UUID[] = "06d1e5e7-79ad-4a71-8faa-373789f7d93c";
inline constexpr char CCCD_UUID[] = "00002902-0000-1000-8000-00805f9b34fb";

// Message framing: 0x40 0x2B <cmd> <'0'+len> <payload...> 0x25
inline constexpr uint8_t MSG_START = 0x40;
inline constexpr uint8_t MSG_FILLER = 0x2B;
inline constexpr uint8_t MSG_END = 0x25;
inline constexpr size_t MAX_PAYLOAD = 16;
inline constexpr size_t MAX_PACKET = MAX_PAYLOAD + 5;

enum Cmd : uint8_t {
  FETCH = 0x46,        // 'F' - request a notification for a given command
  MUTE = 0x4D,         // 'M'
  VOLUME = 0x56,       // 'V'
  SOURCE = 0x49,       // 'I'
  BALANCE = 0x58,      // 'X'
  AV_DIRECT = 0x41,    // 'A'
  BRIGHTNESS = 0x42,   // 'B'
  DAC_PRESENCE = 0x44, // 'D' - "1" means the amp is a ONE HD
  HEADPHONE = 0x48,    // 'H'
  SW_VERSION = 0x54,   // 'T'
  SERIAL_NUMBER = 0x53 // 'S'
};

inline constexpr uint8_t PAYLOAD_BRIGHTNESS_UP = 0x2B;
inline constexpr uint8_t PAYLOAD_BRIGHTNESS_DOWN = 0x2D;

inline constexpr uint8_t VALUE_TRUE[] = {'1'};
inline constexpr uint8_t VALUE_FALSE[] = {'0'};

// Cyrus ONE input indices (1-based), matching the HA integration.
inline constexpr const char *SOURCES_ONE[] = {"Bluetooth", "Phono", "AUX 3",
                                              "AUX 4",    "AUX 5", "AV"};
inline constexpr const char *SOURCES_ONE_HD[] = {"Bluetooth", "USB",  "Optical",
                                                 "SPDIF",     "Phono", "AUX 6",
                                                 "AUX 7",     "AV"};

inline size_t build_message(uint8_t cmd, const uint8_t *payload, size_t len,
                            uint8_t *out) {
  if (len > MAX_PAYLOAD)
    len = MAX_PAYLOAD;
  out[0] = MSG_START;
  out[1] = MSG_FILLER;
  out[2] = cmd;
  out[3] = static_cast<uint8_t>('0' + len);
  std::memcpy(out + 4, payload, len);
  out[4 + len] = MSG_END;
  return len + 5;
}

inline size_t build_volume_packet(int volume, uint8_t *out) {
  const int clamped = volume < 0 ? 0 : (volume > 90 ? 90 : volume);
  const uint8_t payload[2] = {static_cast<uint8_t>('0' + (clamped / 10)),
                              static_cast<uint8_t>('0' + (clamped % 10))};
  return build_message(Cmd::VOLUME, payload, sizeof(payload), out);
}

inline size_t build_balance_packet(int balance, uint8_t *out) {
  const int clamped = balance < 0 ? 0 : (balance > 90 ? 90 : balance);
  const uint8_t payload[2] = {static_cast<uint8_t>('0' + (clamped / 10)),
                              static_cast<uint8_t>('0' + (clamped % 10))};
  return build_message(Cmd::BALANCE, payload, sizeof(payload), out);
}

inline size_t build_source_packet(int index_1_based, uint8_t *out) {
  const uint8_t payload[1] = {static_cast<uint8_t>('0' + index_1_based)};
  return build_message(Cmd::SOURCE, payload, sizeof(payload), out);
}

inline size_t build_bool_packet(uint8_t cmd, bool enabled, uint8_t *out) {
  const uint8_t *payload = enabled ? VALUE_TRUE : VALUE_FALSE;
  return build_message(cmd, payload, 1, out);
}

inline size_t build_brightness_packet(bool up, uint8_t *out) {
  const uint8_t payload[1] = {up ? PAYLOAD_BRIGHTNESS_UP : PAYLOAD_BRIGHTNESS_DOWN};
  return build_message(Cmd::BRIGHTNESS, payload, sizeof(payload), out);
}

inline size_t build_fetch_packet(uint8_t cmd, uint8_t *out) {
  return build_message(Cmd::FETCH, &cmd, 1, out);
}

// Parses a raw notification frame. On success returns the payload length and
// fills cmd/payload; returns 0 for invalid frames.
inline size_t parse_message(const uint8_t *data, size_t len, uint8_t *cmd,
                            uint8_t *payload) {
  if (len < 5 || data[0] != MSG_START || data[len - 1] != MSG_END)
    return 0;
  const size_t payload_len = len - 5;
  *cmd = data[2];
  std::memcpy(payload, data + 4, payload_len);
  return payload_len;
}

}  // namespace protocol

}  // namespace cyrus_ble
