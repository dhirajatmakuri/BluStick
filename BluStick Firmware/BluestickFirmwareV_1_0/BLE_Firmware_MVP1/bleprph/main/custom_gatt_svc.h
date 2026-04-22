#pragma once

#include "host/ble_hs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"


int bluestick_gatt_svc_init(void);
void ble_sniffer_start(void);
void ActivateSearch(void *params);
extern uint16_t current_conn_handle;
extern bool upload_chr_subscribed;
extern uint16_t recieve_chr_val_handle;
extern uint16_t upload_chr_val_handle;
extern void ble_notify(void *param);
extern EventGroupHandle_t search;
#define SEARCH_START_BIT (1 << 0)
#define SEARCH_STOP_BIT (1 << 1)
extern bool isInSearch;
extern char msg[50];