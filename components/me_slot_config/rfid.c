#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pn532.h"

#include "reporter.h"
#include "stateConfig.h"

#include "esp_log.h"
#include "me_slot_config.h"

#define PN532_I2C_ADDRESS 0x24
#define PN532_I2C_NUM I2C_NUM_0
#define PN532_SDA_GPIO 21
#define PN532_SCL_GPIO 22
#define PN532_PACKBUFFSIZ 64
// #define CONFIG_PN532DEBUG 1
// #define CONFIG_MIFAREDEBUG 1
extern uint8_t SLOTS_PIN_MAP[4][3];
extern configuration me_config;
extern stateStruct me_state;

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
static const char *TAG = "RFID";

void init_rfid_slot(int num_of_slot){
    int res = init_PN532_I2C(4,5,0,0,0);
    ESP_LOGD(TAG, "RFID init res:%d", res);

    // uint8_t uid_len = 7;
    // uint8_t uid[uid_len];

    // while(1){
    //     readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uid_len, 0);
    //         ESP_LOGD(TAG, "RFID read: %d:%d:%d:%d:%d", uid[0],uid[1],uid[2],uid[3],uid[4]);
        
    //     vTaskDelay(pdMS_TO_TICKS(500));
    // }

    SAMConfig();

    uint32_t versiondata = getPN532FirmwareVersion();
    if (! versiondata)
    {
        ESP_LOGE(TAG, "Could not find PN532");
        //return;
    }

    ESP_LOGI(TAG, "Found PN532 with firmware version: %d.%d", (versiondata >> 24) & 0xFF, (versiondata >> 16) & 0xFF);

    while (1)
    {
        // Wait for an NFC card
        uint8_t uid[7];
        uint8_t uidLength;
        if (readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 2000))
        {
            ESP_LOGI(TAG, "Found card with UID:");
            for (uint8_t i = 0; i < uidLength; i++)
            {
                ESP_LOGI(TAG, "%02X ", uid[i]);
            }
            ESP_LOGI(TAG, "");
        }
        else
        {
            ESP_LOGI(TAG, "Waiting for card...");
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}