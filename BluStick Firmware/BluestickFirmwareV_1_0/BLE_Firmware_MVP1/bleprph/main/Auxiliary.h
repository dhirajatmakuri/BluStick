#ifndef AUXILIARY_H
#define AUXILIARY_H
#pragma once
#include "esp_log.h"
#include <stdio.h>
#include <math.h>
#include <time.h>

//-------------------------------------KALMAN FILTER STRUCTURE--------------------------------------
typedef struct {
    float q; // process noise covariance
    float r; // measurement noise covariance
    float x; // estimated value
    float p; // estimation error covariance
    float k; // kalman gain
} KalmanFilter1D;

// Initialize Kalman filter
static void kalman_init(KalmanFilter1D *kf, float process_noise, float measurement_noise, float initial_value) {
    kf->q = process_noise;
    kf->r = measurement_noise;
    kf->x = initial_value;
    kf->p = 1.0f;
    kf->k = 0.0f;
}

// Update Kalman filter with new measurement
static float kalman_update(KalmanFilter1D *kf, float measurement) {
    // Prediction update
    kf->p = kf->p + kf->q;
    
    // Measurement update
    kf->k = kf->p / (kf->p + kf->r);
    kf->x = kf->x + kf->k * (measurement - kf->x);
    kf->p = (1.0f - kf->k) * kf->p;
    
    return kf->x;
}

// Adaptively adjust Kalman parameters based on RSSI strength
static void kalman_adapt_to_rssi(KalmanFilter1D *kf, int8_t rssi) {
    // Adjust measurement noise (r) based on signal strength
    // Weaker signals are noisier and need more filtering
    if (rssi > -60) {
        // Close range: low noise, can be more responsive
        kf->q = 4.0f;
        kf->r = 20.0f;
    } else if (rssi > -75) {
        // Medium range: moderate noise
        kf->q = 3.0f;
        kf->r = 28.0f;
    } else {
        // Far range: high noise, need heavy filtering
        kf->q = 2.0f;
        kf->r = 40.0f;
    }
}

//-------------------------------------RSSI FILTERING--------------------------------------
static void moving_average_filter(int8_t* rssi, int8_t current_rssi,size_t N ){
    float alpha = 2.0/(1.0+N);
    if(*rssi == 0){
        *rssi = current_rssi;
    }
    else{
      *rssi = round(((float)current_rssi * alpha) + ((float)*rssi * (1-alpha)));  
    }
    
}

static double distance_conversion(int8_t rssi){
    // Measured TX power at 1 meter reference distance
    // For devices transmitting at -2 to +1 dBm, typical RSSI at 1m is around -50 to -55 dBm
    int8_t tx_pwr_at_1m = -55;  // Calibrated reference RSSI at 1 meter
    
    // Path loss exponent (n):
    // 2.0 = free space
    // 2.0-2.5 = open office, home
    // 2.5-3.0 = office with obstacles
    // 3.0-4.0 = indoor with walls/interference
    double n = 2.6;  // Realistic indoor environment
    
    // Handle edge cases
    if (rssi > tx_pwr_at_1m) {
        // Device is likely closer than 1 meter or signal unusually strong
        return 0.5;  // Return minimum distance
    }
    if (rssi < -100) {
        // Signal too weak for reliable distance
        return 50.0;  // Return maximum reasonable distance
    }
    
    // Path loss formula: RSSI = TxPower - 10*n*log10(distance)
    // Rearranged: distance = 10^((TxPower - RSSI) / (10*n))
    double exponent = ((double)(tx_pwr_at_1m - rssi)) / (10.0 * n);
    double distance = pow(10.0, exponent);
    
    // Clamp to reasonable range
    if (distance < 0.5) distance = 0.5;
    if (distance > 50.0) distance = 50.0;

    return distance;
}

//-------------------------------------BLE SNIFFER DATA STRUCTURE--------------------------------------
// IMPORTANT: This structure is sent over BLE
// The Kalman filter at the end is NOT transmitted to the client
typedef struct {
    char mac_addr[30];
    int8_t curr_rssi;
    int8_t rssi;
    uint32_t timestamp;
    float distance;
    char uuid_str[40];
    // Kalman filter - placed at end, NOT transmitted over BLE
    KalmanFilter1D kf;
} ble_sniffer_data_t;

//Hashmap Setup
typedef struct pair{
    char*  key;
    ble_sniffer_data_t* data;
    uint32_t hit_count;
    uint32_t access_timestamp;
} Pair;

typedef struct hashmap {
    Pair** devices_list;
    unsigned int cap;
    unsigned int len;
} HashMap;

//creates a new empty HashMap
HashMap* newHashMap(){
    HashMap* this = malloc(sizeof(HashMap));
    this->cap = 100;
    this->len = 0;
    this->devices_list = calloc((this->cap),sizeof(Pair*));
    return this;
}

//hashing function from djb2 hash
unsigned long mac_hash(const char* mac_addr){
    unsigned long hash = 5381;
    int c;
    while((c = *mac_addr++)){
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

//assumes a completely filled list and right now only using LRU
unsigned int rank(HashMap* map){
    int candidate_index = 0;

    for(int i = 0; i< map->cap; i++){
        //check if spot is empty just incase: 
        if(map->devices_list[i] == NULL){
            return i;
        }
        //chooses the device with lowest time value indicating it hasn't been accessed in a while
        if( map->devices_list[candidate_index]->access_timestamp > map->devices_list[i]->access_timestamp){
            candidate_index = i;
        }
    }
    return candidate_index;
}  


void hashmap_insert(HashMap *map, const char* mac_addr, const ble_sniffer_data_t* data){
    unsigned int index = mac_hash(mac_addr) % map->cap;
    unsigned int initial_index = index;
   // ESP_LOGI("HASHMAP","mac addr: %s, initial index from hash: %d",mac_addr,index);
    //chooses the next available spot if a collision is detected assuming space is available
    Pair* pair = map->devices_list[index];

    //copy data and exits if we find a duplicate 
    while(pair){
        if(strcmp(pair->key, mac_addr) == 0){
            memcpy(pair->data,data,sizeof(ble_sniffer_data_t));
            pair->hit_count++;
            pair->access_timestamp = (uint32_t) time(NULL);
            return;
        }
        index = (index+1) % map->cap;
        //gets vicitim to kick and exits if there are no free spaces
        if(index == initial_index){ break; }
        pair = map->devices_list[index];
    }
    //if full we do replacement
    if(map->len == map->cap){
        index = rank(map);
    }
    //if spot is already filled (meaning we used replacement policy to get a candidate) or we found a similar mac
    if(map->devices_list[index]){
        free(map->devices_list[index]->data);
        free(map->devices_list[index]->key);
        free(map->devices_list[index]);
        map->len--;
    }
    //initializes and adds new entry by copying 
    //ESP_LOGI("HASHMAP","mac addr: %s, being inserted at index: %d",mac_addr,index);
    Pair *new_pair = malloc(sizeof(Pair));
    new_pair->key = strdup(mac_addr);
    new_pair->data = malloc(sizeof(ble_sniffer_data_t));
    new_pair->data->rssi = new_pair->data->curr_rssi;
    new_pair->data->distance = distance_conversion(new_pair->data->rssi);
    memcpy(new_pair->data,data,sizeof(ble_sniffer_data_t));
    new_pair->hit_count = 1;
    new_pair->access_timestamp = (uint32_t) time(NULL);
    map->devices_list[index] = new_pair;
    map->len++;
}

ble_sniffer_data_t* hashmap_get(HashMap *map, const char* mac_addr){
    unsigned int index = mac_hash(mac_addr) % map->cap;
    unsigned int initial_index = index;
    uint32_t current_time = time(NULL);

    while(map->devices_list[index] != NULL){
        if(strcmp(map->devices_list[index]->key, mac_addr) == 0){
            // ESP_LOGI("HASHMAP","MAC found in here: %s",mac_addr);
            //updates the hit and access time
            Pair* pair = map->devices_list[index];
            pair->access_timestamp = current_time;
            pair->hit_count++;
            return pair->data;
        }
        index = (index+1) % map->cap;
        if(map->devices_list[index] == NULL || initial_index == index){
            return NULL;
     }  
    }
    return NULL;
}

//clears out the hashmap
void hashmap_clear(HashMap* map){
    for(int i = 0; i < map->cap; i++){
        if(map->devices_list[i]){
          ESP_LOGI("HASHMAP","freeing index: %d of HashMap",i);
          free(map->devices_list[i]->data);
          free(map->devices_list[i]->key);
          free(map->devices_list[i]);
          map->devices_list[i] = NULL;
        }
    }
    map->len = 0;
}


#endif