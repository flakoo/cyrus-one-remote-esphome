# Cyrus ONE Remote via ESPHome

Control a **Cyrus ONE** or **Cyrus ONE HD** amplifier from Home Assistant
through a dedicated ESP32 (M5Stack Atom S3U) acting as a permanent BLE↔WiFi
relay. No Home Assistant Bluetooth adapter required — the ESP32 sits next to
the amplifier and owns the single BLE connection.

## Why

- The Cyrus amplifiers power on at **volume 0** (no memory of the last
  setting) and their BLE stack has a two-stage boot: it first advertises a
  temporary MAC, then ~4 s later the real one. The MACs change on every
  power cycle; only the advertised name prefix `ONE-` is stable.
- Driving BLE directly from Home Assistant (e.g. the
  [cyrus-one-hass](https://github.com/vitkuv/cyrus-one-hass) integration)
  means random connect times of ~7-40 s after power-on — too late and too
  unpredictable for setting the volume before the first audible note.

This project moves the whole BLE link onto the ESP32. After power-on the
relay connects deterministically (~8 s), writes the startup volume, and
**stays connected**, exposing the amplifier's state and controls to Home
Assistant over the (encrypted) ESPHome native API.

## What you get in Home Assistant

Once the ESP32 device is added to HA via the standard ESPHome integration,
the following native entities appear — no custom integration needed:

- **Cyrus Volume** — slider 0-100 % (the amp's non-linear volume
  characteristic, curve factor 15, is applied on the ESP32)
- **Cyrus Source** — dropdown (union of ONE / ONE HD inputs; entries the
  current model does not have are ignored)
- **Cyrus Mute**, **Cyrus AV Direct** — switches
- **Cyrus Balance** — slider 0-90
- **Cyrus Brightness Up/Down** — buttons (display LEDs)
- Sensors: raw volume, balance, source, model, firmware version, serial
- Binary sensors: **Cyrus Connected**, **Cyrus Ready** (link up + startup
  volume written), **Cyrus Volume Set** (legacy pulse used by the power-on
  automation), headphones present

Physical knob/source changes on the amp itself are pushed live to HA over
BLE notifications.

## Repository layout

```
esphome/
  cyrus-remote.yaml        device configuration (M5Stack Atom S3U)
  secrets.yaml.example     copy to secrets.yaml and fill in
  components/cyrus_ble/    the custom component (NimBLE, no ESPHome BLE stack)
home-assistant/
  cyrus_remote.yaml        HA package: power-on automation + startup volume
```

## Setup

1. ESP32: copy `esphome/secrets.yaml.example` to `esphome/secrets.yaml`,
   fill in Wi-Fi + API/OTA secrets, compile and flash
   `esphome/cyrus-remote.yaml`.
2. Add the device to Home Assistant (Settings → Devices & Services →
   ESPHome) — all entities appear automatically.
3. Home Assistant: include `home-assistant/cyrus_remote.yaml` as a package
   and replace `switch.amplifier_plug` with your smart plug entity.
4. Set the default startup volume on the **Cyrus startup volume** helper
   (0-90; 36 ≈ -40 dB).

## How the power-on sequence works

1. Smart plug turns on (from HA or physically).
2. HA calls `esphome.<device>_set_startup_volume`.
3. The ESP32 scans for `ONE-*`, waits out the temporary boot MAC, connects
   to the stable MAC, writes the volume, subscribes to state notifications.
4. `Cyrus Ready` turns on; all control entities become usable.
5. If the BLE link drops while the amp is still on, the ESP32 rescans and
   reconnects automatically.

## Status LED (M5Stack Atom S3U)

- cyan pulse — boot / connecting to Wi-Fi / HA
- blue pulse — scanning for the amplifier
- green — BLE link up
- white pulse — startup volume written
- red pulse — failure (scan timeout, connect/write error)

## Relationship to other projects

- Replaces [ha-cyrus-one-volume-init](https://github.com/flakoo/ha-cyrus-one-volume-init),
  which only wrote the startup volume and disconnected to let the HA BLE
  integration take over.
- Protocol constants and quirks (e.g. the MTU "Unlikely error" on writes)
  come from [cyrus-one-hass](https://github.com/vitkuv/cyrus-one-hass).
