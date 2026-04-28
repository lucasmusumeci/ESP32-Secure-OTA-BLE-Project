#include "main.h"

void init_led_strip() {
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_NUM,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, 
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, 
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

void update_led_color(uint8_t r, uint8_t g, uint8_t b) {
    if (led_strip) {
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
    }
}

// --- NVS Encryption ---
esp_err_t init_encrypted_nvs() {
    // Find the partition named "nvs_key"
    const esp_partition_t* key_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 
        ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, 
        "nvs_key"
    );

    if (key_part == NULL) {
        ESP_LOGE("NVS", "nvs_key partition not found in partition table!");
        return ESP_FAIL;
    }

    // Read the security configuration (keys) from that partition
    nvs_sec_cfg_t cfg;
    esp_err_t err = nvs_flash_read_security_cfg(key_part, &cfg);
	if ( err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED ) {
		//cFirst boot : generate and store keys
		ESP_ERROR_CHECK ( nvs_flash_generate_keys ( key_part , & cfg ) ) ;
	} else {
		ESP_ERROR_CHECK ( err ) ;
	}

    // Use the SECURE init function instead of the standard one
    err = nvs_flash_secure_init(&cfg);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_secure_init(&cfg);
    }
    
    return err;
}

// --- NimBLE GATT Configuration ---

// Callback when phone writes to the RGB Characteristic
static int gatt_svr_chr_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len == 3) {
            uint8_t rgb[3];
            ble_hs_mbuf_to_flat(ctxt->om, rgb, 3, NULL);
            ESP_LOGI(TAG, "NimBLE Received RGB: R=%d, G=%d, B=%d", rgb[0], rgb[1], rgb[2]);
            update_led_color(rgb[0], rgb[1], rgb[2]);
        } else {
            ESP_LOGW(TAG, "Invalid Write Length: %d", len);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// --- GAP Advertising ---
void ble_app_advertise(void) {
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "BLE Connected");
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE Disconnected; restarting advertising");
            ble_app_advertise();
            break;
    }
    return 0;
}

void ble_app_on_sync(void) {
    uint8_t addr_type;
    ble_hs_id_infer_auto(0, &addr_type);
    ble_app_advertise();
}

void host_task(void *param) {
    nimble_port_run(); // This function only returns when nimble_port_stop() is called
    nimble_port_deinit();
}

// --- WiFi ---
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xTaskCreate(&ota_update_task, "ota_update_task", 16384, NULL, 5, NULL);
    }
}

void wifi_init_sta(void) {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

	wifi_config_t wifi_config = {
	    .sta = {
	        .ssid = CONFIG_ESP_WIFI_SSID,     // This pulls from menuconfig
	        .password = CONFIG_ESP_WIFI_PASSWORD, // This pulls from menuconfig
	        /* Setting threshold ensures we don't connect to weak open networks */
	        .threshold.rssi = -127,
	        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
	    },
	};
	
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// --- HTTP ---
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    response_info_t *resp = (response_info_t *)evt->user_data;

    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // Check for buffer overflow
            if (resp->len + evt->data_len < sizeof(resp->buffer)) {
                memcpy(resp->buffer + resp->len, evt->data, evt->data_len);
                resp->len += evt->data_len;
                resp->buffer[resp->len] = '\0'; // Always keep it null-terminated
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI("HTTP", "Disconnected");
            break;
        default:
            break;
    }
    return ESP_OK;
}

// --- OTA ---
bool is_newer(const char* running, const char* incoming) {
    int r_major, r_minor, r_patch;
    int i_major, i_minor, i_patch;

    sscanf(running, "%d.%d.%d", &r_major, &r_minor, &r_patch);
    sscanf(incoming, "%d.%d.%d", &i_major, &i_minor, &i_patch);

    if (i_major > r_major) return true;
    if (i_major < r_major) return false;
    if (i_minor > r_minor) return true;
    if (i_minor < r_minor) return false;
    return (i_patch > r_patch);
}

void ota_update_task(void *pvParameter) {
	
	// Gets app description and prints version number
	const esp_app_desc_t *app_desc = esp_app_get_description();
	ESP_LOGI("OTA", "Hello from version %s !", app_desc->version);
    ESP_LOGI(TAG, "Starting Secure OTA update...");

	// Define the HTTP configuration
    esp_http_client_config_t http_config = {
        .url = "https://192.168.0.128:8070/update.bin",
        .cert_pem = (const char *)server_crt_start,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
        .skip_cert_common_name_check = true, // We don't check the CA field
    };
	
	// Wrap it in the OTA configuration structure
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
		.partial_http_download = true,       // Required to read header first
		.max_http_request_size = 64 * 1024,  // Max request size (64KB)
    };

	esp_https_ota_handle_t https_ota_handle = NULL;
	    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
	    if (err != ESP_OK) {
	        ESP_LOGE("OTA", "ESP HTTPS OTA Begin failed");
	        vTaskDelete(NULL);
	        return;
	    }

	// Get the description of the image on the server (first chunk)
    esp_app_desc_t incoming_app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &incoming_app_desc);
    if (err == ESP_OK) {
        const esp_app_desc_t *running_app_desc = esp_app_get_description();
        ESP_LOGI("OTA", "Running version: %s | Incoming version: %s", 
                 running_app_desc->version, incoming_app_desc.version);
	    // Version Check: If versions match, stop immediately
        if (!is_newer(running_app_desc->version, incoming_app_desc.version)) {
            ESP_LOGW("OTA", "Incoming version is not new. Aborting update.");
            esp_https_ota_abort(https_ota_handle);
            vTaskDelete(NULL);
            return;
        }
    }
	
	// If incoming version is newer: Proceed with the rest of the chunks
    ESP_LOGI("OTA", "New version detected. Downloading...");
    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        
        // Log progress or feed watchdog
        ESP_LOGD("OTA", "Bytes written: %d", esp_https_ota_get_image_len_read(https_ota_handle));
    }
    if (esp_https_ota_finish(https_ota_handle) == ESP_OK) {
        ESP_LOGI("OTA", "OTA upgrade successful! Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE("OTA", "OTA upgrade failed!");
    }

    vTaskDelete(NULL);
}

void app_main(void) {
	// Initialize NVS
    esp_err_t ret = init_encrypted_nvs();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = init_encrypted_nvs();
    }
    ESP_ERROR_CHECK(ret);

	// Initialize LED
    init_led_strip();

	// Initialize NimBLE
	ret = nimble_port_init();
	if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init nimble %d", ret);
        return;
    }
	    
    // Configure GATT and GAP
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svr_svcs);
    ble_gatts_add_svcs(gatt_svr_svcs);

    // Set the sync callback
    ble_hs_cfg.sync_cb = ble_app_on_sync;

    // Start the NimBLE Host Task manually
    xTaskCreate(host_task, "nimble_host", 4096, NULL, 5, NULL);
	
	// Mark the firmware as "valid"
	esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
	if (err == ESP_OK) {
	    ESP_LOGI("OTA", "App is valid! Rollback cancelled.");
    } else {
        ESP_LOGD("OTA", "Note: esp_ota_mark_app_valid_cancel_rollback returned %s", esp_err_to_name(err));
    }

    // WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));
    
    wifi_config_t wifi_config = {
        .sta = { .ssid = CONFIG_ESP_WIFI_SSID, .password = CONFIG_ESP_WIFI_PASSWORD },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}