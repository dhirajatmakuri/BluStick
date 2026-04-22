// main.c
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_peripheral_prph";

#define DEVICE_NAME   "ESP32_BLUESTICK"
#define SVC_UUID16    0x00FF
#define CHAR_UUID16   0xFF01

static int g_conn_handle = -1;
static uint16_t g_char_handle = 0;
static bool g_notify_enabled = false;

/* ---------- GATT service definition (1 service, 1 characteristic) ---------- */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(SVC_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(CHAR_UUID16),
                .access_cb = NULL,                       // no read callback; static notify only
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_char_handle,
            },
            { 0 }
        },
    },
    { 0 }
};

/* ---------- GAP event callback ---------- */
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                g_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "Connected, handle=%d", g_conn_handle);
            } else {
                ESP_LOGW(TAG, "Connect failed; status=%d", event->connect.status);
                /* restart advertising */
                struct ble_gap_adv_params adv_params = {0};
                adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
                adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
                ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected; reason=%d. Restarting advertise.", event->disconnect.reason);
            g_conn_handle = -1;
            g_notify_enabled = false;
            /* restart advertising */
            {
                struct ble_gap_adv_params adv_params = {0};
                adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
                adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
                int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);
                if (rc) {
                    ESP_LOGW(TAG, "ble_gap_adv_start rc=%d", rc);
                }
            }
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            

            if (event->subscribe.attr_handle == g_char_handle) {
                g_notify_enabled = event->subscribe.cur_notify;
                ESP_LOGI(TAG, "Subscribe event: notifications %s", g_notify_enabled ? "enabled" : "disabled");
            }
            break;
        case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update event; conn_handle=%d mtu=%d",
             event->mtu.conn_handle,
             event->mtu.value);
         break;

        default:
            break;
    }
    return 0;
}

/* ---------- advertise configuration and start ---------- */
static void start_advertise(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_gap_adv_params adv_params;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "Advertising as '%s'", DEVICE_NAME);
    } else {
        ESP_LOGE(TAG, "ble_gap_adv_start failed rc=%d", rc);
    }
}

/* ---------- NimBLE host task ---------- */
static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ---------- NimBLE sync and reset callbacks (pattern from esp example) ---------- */
static void ble_app_on_sync(void)
{
    /* set device name and init GATT */
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_svc_gatt_init();

    /* add services */
    ble_gatts_count_cfg(gatt_svr_svcs);
    ble_gatts_add_svcs(gatt_svr_svcs);

    /* begin advertising */
    start_advertise();
}

static void ble_app_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset; reason=%d", reason);
}

/* ---------- init NimBLE and controller (ESP-IDF v5.5.1 pattern) ---------- */
static void ble_nimble_init(void)
{
    esp_err_t ret;

    /* release classic bt if not used */
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Released Classic BT memory");
    } else {
        ESP_LOGW(TAG, "esp_bt_controller_mem_release returned %s", esp_err_to_name(ret));
    } 

    /* host callbacks from example */
    ble_hs_cfg.reset_cb = ble_app_on_reset;
    ble_hs_cfg.sync_cb  = ble_app_on_sync;


    //sets the config to bluetooth only 
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
   

    ret = esp_bt_controller_init(&bt_cfg);

    ESP_LOGI(TAG, "esp_bt_controller_init OK");


    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %s", esp_err_to_name(ret));
        return;
    }

    /* init nimble host stack if this fails it usually means that controller was not setup or som errror relating to it  */
    ret = nimble_port_init();
    if (ret) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return;
    }

    /* start host task */
    nimble_port_freertos_init(host_task);
    
    ESP_LOGI(TAG, "NimBLE initialized");
}

/* SENDS THE DATA TO THE HOST (PHONE OR DEVICE THAT WANTS THE DATA)*/
static void notify_task(void *arg)
{
    (void)arg;
    char buf[128];

    while (1) {
        if (g_conn_handle >= 0 && g_notify_enabled && g_char_handle != 0) {
            uint64_t ts = (uint64_t)esp_timer_get_time();
            int len = snprintf(buf, sizeof(buf),
                "%" PRIu64 ",TEST,0,-50,AA:BB:CC:11:22:33,\"testssid\"",
                ts);

            struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
            if (om) {
                int rc = ble_gatts_notify_custom(g_conn_handle, g_char_handle, om);
                if (rc == 0) {
                    ESP_LOGI(TAG, "Notified: %s", buf);
                } else {
                    ESP_LOGW(TAG, "ble_gatts_notify_custom rc=%d", rc);
                }
            } else {
                ESP_LOGW(TAG, "mbuf allocation failed");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ble_nimble_init();

    xTaskCreate(notify_task, "notify_task", 4096, NULL, 5, NULL);
}
