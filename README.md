# ESP32-S3 Secure Embedded System

A production-oriented embedded project built on the **ESP32-S3** (ESP-IDF v6.0) demonstrating a full IoT security stack, secure OTA firmware updates, and a BLE-controlled RGB LED with an Android companion app.

---

## Overview

| Domain | Stack |
|---|---|
| Firmware | C · ESP-IDF v6.0 · FreeRTOS |
| Security | Flash Encryption (AES-XTS) · NVS Encryption · Secure Boot V2 (RSA-3072) |
| OTA | `esp_https_ota` · Python HTTPS server · Semantic versioning · Rollback protection |
| BLE | NimBLE · GATT server · Custom 128-bit service/characteristic |
| Android | Kotlin · Jetpack Compose · Android BLE API |
| LED | WS2812B · RMT peripheral · `led_strip` component |

---

## Repository Structure

```
.
├── firmware/
│   ├── main/
│   │   ├── main.c          # App entry point — NVS, BLE, WiFi, OTA tasks
│   │   ├── main.h          # GATT definitions, includes, prototypes
│   │   ├── CMakeLists.txt  # Component registration + cert embedding
│   │   ├── Kconfig.projbuild
│   │   ├── idf_component.yml
│   │   └── server.crt      # TLS cert embedded at build time (not tracked)
│   ├── CMakeLists.txt
│   └── partitions.csv      # Dual OTA slot layout
├── OTA_server/
│   └── firmware_server.py  # Self-signed HTTPS server with Range support
└── ESP32_RGB_LED/          # Android companion app (Jetpack Compose)
    └── app/src/main/java/com/example/esp32_rgb_led/
        ├── MainActivity.kt
        └── BleManager.kt
```

---

## Security Architecture

Three complementary layers form a hardware-rooted chain of trust:

### 1 · Flash Encryption (AES-XTS)
All app partitions, `otadata`, and `nvs_keys` are encrypted in hardware by the ESP32-S3's flash controller. The AES key is generated on first boot, burned into eFuses, and never extractable. Raw bytes read from the flash chip are ciphertext — protecting against physical cloning attacks.

```
# After enabling flash encryption, always use:
idf.py encrypted-flash
```

### 2 · NVS Encryption (AES-XTS)
A dedicated `nvs_keys` partition (itself flash-encrypted) stores the AES-XTS key pair. On every boot the firmware detects first-run automatically and generates keys if needed:

```c
esp_err_t err = nvs_flash_read_security_cfg(key_part, &cfg);
if (err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
    nvs_flash_generate_keys(key_part, &cfg);
}
nvs_flash_secure_init(&cfg);
```

All subsequent `nvs_get_*()` / `nvs_set_*()` calls are transparently encrypted — no application code changes required.

### 3 · Secure Boot V2 (RSA-3072)
The boot ROM verifies the bootloader signature; the bootloader verifies the app image signature. The private signing key **never resides on the device** — only the SHA-256 digest of the public key is burned into eFuses.

```bash
# Generate signing key (keep offline / in an HSM)
idf.py secure-generate-signing-key --version 2 --scheme rsa3072 secure_boot_private_key.pem

# Sign a binary manually (build-once, sign-once, deploy-to-all workflow)
espsecure.py sign_data --version 2 --keyfile secure_boot_private_key.pem \
    --output signed_firmware.bin build/firmware.bin
```

> **Warning:** once Secure Boot eFuses are burned the configuration is permanent. Never enable *Allow potentially insecure options* in production builds.

---

## OTA Firmware Updates

### Partition layout (`partitions.csv`)

```
nvs,        data,  nvs,      0xe000, 16K
otadata,    data,  ota,              8K
nvs_key,    data,  nvs_keys,         4K,   encrypted
ota_0,      app,   ota_0,            1800K
ota_1,      app,   ota_1,            1800K
```

Two equal app slots allow the bootloader to roll back to the previous firmware if the new image crashes before calling `esp_ota_mark_app_valid_cancel_rollback()`.

### Server

```bash
cd OTA_server
# Generate a self-signed certificate (development only)
openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt \
    -days 365 -nodes -subj "/CN=OTA-Server"

python firmware_server.py   # HTTPS on port 8070 with Range-request support
```

### Client flow

1. Connect to server over TLS — certificate verified against the `server.crt` embedded in firmware at build time (`target_add_binary_data`).
2. Fetch only the first 64 KB chunk → extract `esp_app_desc_t` → **semantic version check** (`MAJOR.MINOR.PATCH`). Abort immediately if not newer.
3. Download remaining chunks with `esp_https_ota_perform()` — written directly to the inactive slot, no large RAM buffer needed.
4. `esp_https_ota_finish()` verifies SHA-256 hash (+ RSA signature if Secure Boot is active).
5. Reboot → call `esp_ota_mark_app_valid_cancel_rollback()` early in `app_main()` to confirm health.

---

## BLE — RGB LED Control

### GATT hierarchy

```
Generic Attribute Profile (GATT)
└── Primary Service       4fafc201-1fb5-459e-8fcc-c5c9c331914b
    └── Characteristic    beb5483e-36e1-4688-b7f5-ea07361b26a8
            Flags : READ | WRITE
            Value : 3 bytes  [ R ][ G ][ B ]   (uint8, 0–255)
```

Writing 3 bytes to the characteristic triggers `gatt_svr_chr_handler()`, which calls `led_strip_set_pixel()` + `led_strip_refresh()` immediately. The device re-advertises automatically on disconnection.

### Hardware

| Parameter | Value |
|---|---|
| LED model | WS2812B |
| GPIO | 48 |
| Driver | RMT peripheral via `led_strip` component |
| Format | GRB (hardware order) |

### Android App

The Jetpack Compose companion app (`ESP32_RGB_LED/`) handles:
- Runtime permission requests (`BLUETOOTH_SCAN` / `BLUETOOTH_CONNECT` on API 31+)
- BLE device scan and connection
- RGB colour picker with live preview — three sliders send a write on every value change
- Android 13+ (API 33) BLE write API with graceful fallback for older devices

---

## Build

```bash
# Set target
idf.py set-target esp32s3

# Configure WiFi credentials and security options
idf.py menuconfig

# Build and flash
idf.py build
idf.py flash monitor
```

The TLS certificate must be placed at `firmware/main/server.crt` before building — it is embedded as a binary blob by `target_add_binary_data` in `CMakeLists.txt`.
