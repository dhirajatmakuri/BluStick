#include "host/ble_hs.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_log.h"
#include "custom_gatt_svc.h"
#include "nimble/nimble_port.h"
#include "esp_bt.h"
#include "host/ble_gap.h"
#include "esp_wifi.h"
#include "Auxiliary.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

static const char *BLETAG = "BLE_SNIFFER";
static const char *TAG = "CUSTOM_GATT_MESSANGER";
#define NULL_MAC "00:00:00:00:00:00"
#define MAX_MSG_LEN 50
#define Max_WhiteList 200
#define Notify_interval_ms 100
// Size of data to transmit (excluding Kalman filter at the end)
#define BLE_DATA_SIZE (sizeof(ble_sniffer_data_t) - sizeof(KalmanFilter1D))
uint16_t upload_chr_val_handle = 0;
uint16_t recieve_chr_val_handle = 0;
uint16_t current_conn_handle = 0;
bool upload_chr_subscribed = false;
static uint8_t BlueStickIdentifier = 1;  
char msg[MAX_MSG_LEN];
char whitelist[Max_WhiteList][18];
int whitelist_index;
int whitelist_len;
bool isInSearch;
EventGroupHandle_t search; 

//-------------------------------------BLE SNIFFER CODE ----------------------------------------------
HashMap* device_map;

// converts the BLE MAC address to a string
static void addr_to_str(const ble_addr_t *addr, char *out)
{
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
                addr->val[5], addr->val[4],
                addr->val[3], addr->val[2],
               addr->val[1], addr->val[0]);
}
static void addr_to_str_bytes(uint8_t* addr, char* out){
      sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
                addr[0], addr[1],
                addr[2], addr[3],
               addr[4], addr[5]);
}

void DeactivateSearch(){
    xEventGroupSetBits(search, SEARCH_STOP_BIT);
     snprintf(msg, sizeof(msg), "NULL");
}

void ActivateSearch(void *params){      
    while(1){
        xEventGroupWaitBits(search,SEARCH_START_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        isInSearch = true;
        ESP_LOGI("BUTTON","SEARCH SET TO %d",isInSearch);   
        hashmap_clear(device_map);
        // waits 30 seconds as a test TODO: change this to 30 min or max search time)
        EventBits_t bits = xEventGroupWaitBits(search, SEARCH_STOP_BIT, pdTRUE,pdFALSE, pdMS_TO_TICKS(300000));

        if(bits & SEARCH_STOP_BIT){
            ESP_LOGI("SEARCH","MANUAL STOP INITIATED");
            xEventGroupClearBits(search,SEARCH_STOP_BIT);
        }
        else{
            ESP_LOGI("SEARCH","TIME LIMIT REACHED EXITING SEARCH");     
        }
        isInSearch = false;
        hashmap_clear(device_map);
    }
}


static ble_sniffer_data_t data;
//TODO: add MAC refreshing during a search
/* THE TX POWER SENT BACK FROM THE DEVICE IS THERE TX POWER RELATIVE TO THE DEVICE WE WILL NEED
TO FIND A WAY TO ESTIMATE TX POWER */
// scan callback function 
static int ble_sniffer_gap_event(struct ble_gap_event* event, void* arg){
    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
        {
            struct ble_hs_adv_fields fields;
            int rc = ble_hs_adv_parse_fields(&fields,event->disc.data, event->disc.length_data);
           
            if (rc == 0) {

                char scanned_mac_addr[30] = "";
                data.timestamp = (uint32_t) time(NULL);
                data.curr_rssi = event->disc.rssi;

                addr_to_str(&event->disc.addr,scanned_mac_addr);
                memcpy(data.mac_addr,scanned_mac_addr,sizeof(scanned_mac_addr));

                ble_sniffer_data_t* existing_data = hashmap_get(device_map,scanned_mac_addr);
                if(existing_data != NULL){
                   // ESP_LOGI("HASHMAP","UPDATE AT MAC: %s",existing_data->mac_addr);
                   memcpy(existing_data->mac_addr,scanned_mac_addr,sizeof(scanned_mac_addr));
                   existing_data->timestamp = data.timestamp;
                   existing_data->curr_rssi = data.curr_rssi;
                   
                   // Apply Kalman filter first for noise reduction
                   float kalman_filtered_rssi = kalman_update(&(existing_data->kf), (float)data.curr_rssi);
                   
                   // Then apply moving average on the Kalman-filtered value for additional smoothing
                   moving_average_filter(&(existing_data->rssi), (int)kalman_filtered_rssi, 5);
                   
                   existing_data->distance = (float)distance_conversion(existing_data->rssi);
                }
                else if(isInSearch){
                    for(int i = 0; i < whitelist_len; i++){
                        if(strcmp(whitelist[i],scanned_mac_addr) == 0){
                            // Initialize Kalman filter for new device
                            // q=2.0 (process noise), r=10.0 (measurement noise), initial value=curr_rssi
                            kalman_init(&(data.kf), 4.0f, 35.0f, (float)data.curr_rssi);
                            data.rssi = data.curr_rssi; // Initialize rssi
                            
                            hashmap_insert(device_map,scanned_mac_addr,&data);
                            ESP_LOGI("COMPARE","SIMILAR MACS FOUND");
                            break;
                        }
                    }
                }
                else if(!isInSearch){
                    // Initialize Kalman filter for new device
                    kalman_init(&(data.kf), 4.0f, 35.0f, (float)data.curr_rssi);
                    data.rssi = data.curr_rssi;
                    
                    hashmap_insert(device_map,scanned_mac_addr,&data);
                }
                
                //DEBUG: distance estimation and mac addresses reading testing (allows us to focus on one device for now)
                if(existing_data != NULL && fields.uuids16 != NULL &&fields.uuids16[0].value == 0x4444) {
                    //check mac against the actual mac we are detecting for a sanity check
                    ESP_LOGI("SNIF","MAC: %s", existing_data->mac_addr);
                    ESP_LOGI("SNIF", "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                        event->disc.addr.val[5], event->disc.addr.val[4],
                        event->disc.addr.val[3], event->disc.addr.val[2],
                        event->disc.addr.val[1], event->disc.addr.val[0]);
                    if (fields.name_len > 0)
                        ESP_LOGI("SNIF", "Name: %.*s", fields.name_len, fields.name);
                    for (int i=0;i<fields.num_uuids16;i++)
                        ESP_LOGI("SNIF", "UUID16: 0x%04X", fields.uuids16[i].value);
                    if (fields.mfg_data_len > 0)
                        ESP_LOGI("SNIF", "Mfg data: %.*s", fields.mfg_data_len, fields.mfg_data);
                    ESP_LOGI("SNIF", "the rssi is %d, current rssi: %d, distance(m): %lf",existing_data->rssi,existing_data->curr_rssi,existing_data->distance);
                    ESP_LOGI("SNIF", "current scanned rssi: %d",data.curr_rssi);
                }
            }
            //send the data to the GATT characteristic so it can be uploaded 
           
            for (int i = 0; i < device_map->cap; i++)
            {
                Pair* device = device_map->devices_list[i];
                if(device){
                    if(isInSearch){
                       // ESP_LOGI("HASHMAP","THIS ENTRY @ %d is filled with mac: %s",i,device_map->devices_list[i]->key);
                   //    ESP_LOGI("HASHMAPTABLE","AT index %d mac_addr: %s",i,device->key); 
                    }
                }
                else{
                   continue; 
                }
            }
            

            return 0;
        }
    }
    return 0;
}

void ble_sniffer_start(void) {
#define SNIFFER_SCAN_DURATION BLE_HS_FOREVER 
    
    //disables wifi to help it perform better
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_wifi_set_mode(WIFI_MODE_NULL);
    
    //defines the parameter for the scan
    struct ble_gap_disc_params params = {
        .itvl = 0x00200, //to calculate the time it's 256 
        .window = 0x0200,
        .filter_policy = 0,
        .limited = 0,
        .passive = 1,
        .filter_duplicates = 0,
     };

     int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, SNIFFER_SCAN_DURATION, &params, ble_sniffer_gap_event, NULL);

     if(rc != 0){
        ESP_LOGE(BLETAG, "failed to start the BLE scan: %d", rc);
     }
     else{
        ESP_LOGI(BLETAG, "BLE sniffer started.");
     }
}

//----------------------------------------------------------------------------------------------------------------------------

/*--------------------------------------------------------------------CHARACTERISTICS AND SERVICES SET UP--------------------------------------------------------------------------*/
/* Define 16-bit UUIDs as static constants */
static const ble_uuid16_t upload_svc_uuid = BLE_UUID16_INIT(0xFFF0);
static const ble_uuid16_t upload_chr_uuid = BLE_UUID16_INIT(0xFFF1);
static const ble_uuid16_t write_svc_uuid = BLE_UUID16_INIT(0xFFF3);
static const ble_uuid16_t recieve_chr_uuid = BLE_UUID16_INIT(0xFFF2);


static int data_upload_characteristic_handler(uint16_t conn_handle, uint16_t attr_handle,struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: {
        //writes data value of the msg buffer to the be sent via blue tooth
        int rc = os_mbuf_append(ctxt->om, msg, strlen(msg));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int recieve_characteristic_handler(uint16_t conn_handle, uint16_t attr_handle,struct ble_gatt_access_ctxt * ctxt, void *arg){
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:{
        int rc = os_mbuf_append(ctxt->om,msg,strlen(msg));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    case BLE_GATT_ACCESS_OP_WRITE_CHR:{
        struct os_mbuf *om = ctxt->om;
        int len = OS_MBUF_PKTLEN(om);

        if(len%6 != 0){
            ESP_LOGI("Search","invalid mac payload length of %d", len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        //parses the list of macs sent as one hexidecimal number
        for(int i = 0; i < len; i += 6){
            uint8_t mac_addr[6];
            os_mbuf_copydata(om,i,6,mac_addr);
            char mac_str[18];
            addr_to_str_bytes(mac_addr,mac_str);
            //exits search if we recieve a null mac = "00:00:00:00:00:00"
            if(strcmp(mac_str,NULL_MAC) == 0){
                ESP_LOGE("SEARCH","EXIT SEARCH MODE NOW");
               DeactivateSearch();
            }
            else{
                //checks if search is active to see whether to start new search or add macs to existing search
                if(isInSearch == false){
                    whitelist_index = 0;
                    whitelist_len = 1;
                    //starts a search event 
                    snprintf(msg, sizeof(msg), "InSearch");
                    xEventGroupSetBits(search, SEARCH_START_BIT);
                    ESP_LOGI("SEARCH","Starting new search event");
                }
                //NOTE: THIS WILL OVERWRITE PREVIOUS MAC ADRESSES IF TOO MANY NEW MACS ARE SENT 
                strcpy(whitelist[whitelist_index],mac_str);
                ESP_LOGE("SEARCH","MAC %s added to the list at index %d",whitelist[whitelist_index],whitelist_index);
                whitelist_index = (whitelist_index + 1)%Max_WhiteList;
                whitelist_len = ((whitelist_len+1) <= Max_WhiteList ) ? whitelist_len+1 : whitelist_len;
            }
            
        }

        return 0;
    }
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}


static const struct ble_gatt_chr_def upload_chr[] = {
    {
                .uuid = &upload_chr_uuid.u,
                .access_cb = data_upload_characteristic_handler,
                .val_handle = &upload_chr_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
    },
    { 0 }
};

static const struct ble_gatt_chr_def write_chr[] = {
            {
                .uuid = &recieve_chr_uuid.u,
                .access_cb = recieve_characteristic_handler,
                .val_handle = &recieve_chr_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
        { 0 }
};

/* Define GATT table similar to the svc example except I use simpler UUID's to stand out */
static const struct ble_gatt_svc_def custom_gatt_svcs[] = {
    //characteristic that handles sending a string of data to the host device
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &upload_svc_uuid.u,
        .characteristics = upload_chr,
    },
    //characteristic that recieves data from the host device as well as having a value the host device can read to ensure data was sent properly 
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &write_svc_uuid.u,
        .characteristics = write_chr,
    },
    { 0 } 
};




// initialization function that registers the function
int bluestick_gatt_svc_init(void)
{
    data.rssi = 0;
    //initially we aren't looking for a particular device 
    isInSearch = 0;

    snprintf(msg, sizeof(msg), "NULL");
    esp_err_t ret = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    if(ret == ESP_OK) ESP_LOGI("SETUP:","TX power set to max");

    //initializes the device map data structure
   device_map =  newHashMap();

    ESP_LOGI(TAG, "Registering custom GATT service: BLUESTICK");

    int rc = ble_gatts_count_cfg(custom_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(custom_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return rc;
    }
    ESP_LOGI(BLETAG,"UPLOAD CHAR HANDLE %d",upload_chr_val_handle);
    ESP_LOGI(BLETAG, "Free heap before scan: %d bytes", esp_get_free_heap_size());
    
    
   return 0;
}

void ble_notify(void *param){
    // Track last sent values to detect changes
    static ble_sniffer_data_t last_sent[200]; // Match Max_WhiteList size
    static bool first_run = true;
    
    if(first_run) {
        memset(last_sent, 0, sizeof(last_sent));
        first_run = false;
    }
    
    while (1)
    {
        if(upload_chr_subscribed && device_map){
            for (int i = 0; i < device_map->cap; i++)
            {
                Pair* device = device_map->devices_list[i];
                
                // Skip empty/NULL entries
                if(device == NULL || device->data == NULL) {
                    continue;
                }
                
                // Validate that MAC address is not empty or null
                if(strlen(device->data->mac_addr) == 0 || strcmp(device->data->mac_addr, NULL_MAC) == 0) {
                //    ESP_LOGW("BLE_NOTIFY", "Skipping device with empty/null MAC at index %d", i);
                    continue;
                }
                
                // Skip devices with invalid/zero RSSI values
                if(device->data->rssi == 0 && device->data->curr_rssi == 0) {
                //    ESP_LOGW("BLE_NOTIFY", "Skipping device %s with zero RSSI", device->data->mac_addr);
                    continue;
                }
                
                // Skip devices with invalid distance (0 or negative)
                if(device->data->distance <= 0.0f) {
              
                    continue;
                }
                
                // Check if data has changed since last transmission
                bool data_changed = false;
                if(last_sent[i].rssi != device->data->rssi ||
                   last_sent[i].curr_rssi != device->data->curr_rssi ||
                   fabsf(last_sent[i].distance - device->data->distance) > 0.01f ||
                   last_sent[i].timestamp != device->data->timestamp ||
                   strcmp(last_sent[i].mac_addr, device->data->mac_addr) != 0) {
                    data_changed = true;
                }
                
                // Skip if data hasn't changed
                if(!data_changed) {
                //    ESP_LOGD("BLE_NOTIFY", "Skipping device %s - no data change", device->data->mac_addr);
                    continue;
                }
                
                int rc;                 
                // Create a NEW mbuf for each attempt
                // Only send the data portion, excluding the Kalman filter
                struct os_mbuf *om = ble_hs_mbuf_from_flat((device->data), BLE_DATA_SIZE);
                if (om == NULL) {
                    continue; // Skip this device and continue with next one
                }
                
                // Shows the hexidecimal structure of the data we are sending (excluding Kalman filter)
                // ESP_LOG_BUFFER_HEX("STRUCT DATA", device->data, BLE_DATA_SIZE);
                
                rc = ble_gatts_notify_custom(current_conn_handle, upload_chr_val_handle, om);
                
                if(rc == BLE_HS_EBUSY){
                    // Wait for the notification to go through if not we keep retrying until we hit our limit
                    // Free the mbuf since notification failed
                    os_mbuf_free_chain(om);
                    vTaskDelay(pdMS_TO_TICKS(50));
                    
                    // Retry once more
                    om = ble_hs_mbuf_from_flat((device->data), BLE_DATA_SIZE);
                    if (om == NULL) {
                    
                        continue;
                    }
                    rc = ble_gatts_notify_custom(current_conn_handle, upload_chr_val_handle, om);
                    if(rc != 0) {
            
                        os_mbuf_free_chain(om);
                        continue;
                    }
                } else if(rc != 0){
        
                    os_mbuf_free_chain(om);
                    continue; // Continue with next device instead of breaking
                } else {
                    // Success - notification queued (mbuf is now owned by stack)
          
                    
                    // Update last_sent tracker after successful transmission
                    memcpy(&last_sent[i], device->data, BLE_DATA_SIZE);
                }
                
                // Small delay between devices to avoid overwhelming the stack
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        // Delay between full cycles through all devices
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay before next transmission cycle
    }
}
