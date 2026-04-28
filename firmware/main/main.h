/*
 * main.h
 *
 *  Created on: 22 Apr 2026
 *      Author: lucas
 */

#ifndef MAIN_MAIN_H_
#define MAIN_MAIN_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

// NimBLE Headers
#include "host/ble_gap.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Wifi and HTTP
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"

// OTA
#include "esp_https_ota.h"
#include "esp_ota_ops.h"

// JSON parsing
#include "cJSON.h"

// LED
#include "led_strip.h"

#define DEVICE_NAME "ESP32_RGB_LED"
static const char *TAG = "OTA_DEVICE";

extern const uint8_t server_crt_start[] asm("_binary_server_crt_start");

// --- LED Configuration ---
#define LED_STRIP_GPIO   48 
#define LED_STRIP_NUM    1 
static led_strip_handle_t led_strip;

void init_led_strip(void);
void update_led_color(uint8_t r, uint8_t g, uint8_t b);

// --- NVS Encryption ---
esp_err_t init_encrypted_nvs(void);

// --- NimBLE GATT Configuration ---
// Service UUID: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
// Characteristic UUID: beb5483e-36e1-4688-b7f5-ea07361b26a8
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f, 0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f);

static const ble_uuid128_t gatt_svr_chr_uuid =
    BLE_UUID128_INIT(0xa8, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7, 0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);

static int gatt_svr_chr_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid = &gatt_svr_chr_uuid.u,
            .access_cb = gatt_svr_chr_handler,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
        }, {
            0, // No more characteristics
        } },
    },
    {
        0, // No more services
    },
};

void ble_app_advertise(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);
void ble_app_on_sync(void);
void host_task(void *param);

// --- WiFi & HTTP ---
// Structure to pass data between the Task and the Event Handler
typedef struct {
    char buffer[1024]; // Adjust size based on your expected JSON size
    int len;
} response_info_t;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void wifi_init_sta(void);
esp_err_t _http_event_handler(esp_http_client_event_t *evt);
void https_post_task(void *pvParameters);

// OTA
bool is_newer(const char* running, const char* incoming); // Returns true if the incoming version is "newer" than the running one
void ota_update_task(void *pvParameter);


#endif /* MAIN_MAIN_H_ */
