/**************************************************************************
 *  @file     pn532_stm32f1.c
 *  @author   Yehui from Waveshare
 *  @license  BSD
 *  
 *  This implements the peripheral interfaces.
 *  
 *  Check out the links above for our tutorials and wiring diagrams 
 *  These chips use SPI communicate.
 *  
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documnetation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to  whom the Software is
 * furished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 **************************************************************************/


#include "stdio.h"
#include <stdlib.h>
#include <string.h>
#include "driver/i2c.h"
#include "pn532.h"
#include "esp_log.h"
#include "esp_err.h"
#include "pn532_i2c_esp32.h"

#define _I2C_ADDRESS                    0x48
#define _I2C_TIMEOUT                    10

#define TAG "PN532"
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#define PN532_I2C_PORT 0
#define I2C_WRITE_TIMEOUT 1000
#define I2C_READ_TIMEOUT 1000

void PN532_Log(const char* log) {
    ESP_LOGD(TAG,"%s", log);
}

/**************************************************************************
 * I2C
 **************************************************************************/
#define PN532_I2C_READ_ADDRESS (0x49)
int i2c_read(uint8_t* data, uint16_t count) {
    ESP_LOGD(TAG, "lets read byte num^%d", count);
    //HAL_I2C_Master_Receive(&hi2c1, _I2C_ADDRESS, data, count, _I2C_TIMEOUT);
    i2c_cmd_handle_t i2ccmd;
    uint8_t *buffer = malloc (count + 3);

    vTaskDelay (10 / portTICK_PERIOD_MS);
    bzero (buffer, count + 3);
    bzero (data, count);

    i2ccmd = i2c_cmd_link_create ();
    i2c_master_start (i2ccmd);
    i2c_master_write_byte (i2ccmd, PN532_I2C_READ_ADDRESS , true); //MAYBE need to increse
    //for (uint8_t i = 0; i < (count + 2); i++)
    for (uint8_t i = 0; i < (count); i++){
        i2c_master_read_byte (i2ccmd, &buffer[i], I2C_MASTER_ACK);
        
    }
    i2c_master_read_byte (i2ccmd, &buffer[count + 1], I2C_MASTER_LAST_NACK);
    i2c_master_stop (i2ccmd);

    if (i2c_master_cmd_begin (PN532_I2C_PORT, i2ccmd, I2C_READ_TIMEOUT / portTICK_RATE_MS) != ESP_OK)
    {
        //Reset i2c bus
        i2c_cmd_link_delete (i2ccmd);
        free (buffer);
        return ESP_FAIL;
    };

    i2c_cmd_link_delete (i2ccmd);

    //memcpy (data, buffer + 1, count);
    memcpy (data, buffer , count);

    for(int i=0; i<count+1; i++){
        printf("%x ", data[i]);
    } 
    printf("\r\n");
    // Start read (n+1 to take into account leading 0x01 with I2C)
    #ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "Reading: ");
    esp_log_buffer_hex(TAG,buffer,count+3);
    #endif
    free (buffer);

    return ESP_OK;
}


int i2c_write(uint8_t* data, uint16_t count) {
    ESP_LOGD(TAG, "lets write byte num^%d", count);
    // for(int i=0; i<count+1; i++){
    //     //printf("%x ", data[i]);
    // } 
    //printf("\r\n");

    i2c_cmd_handle_t i2ccmd = i2c_cmd_link_create ();
    i2c_master_start (i2ccmd);
    //i2c_master_write_byte (i2ccmd, data[0], true);
    i2c_master_write_byte (i2ccmd, _I2C_ADDRESS, true); //MAYBE need to increse
    for (int i = 0; i < count; i++){
        i2c_master_write_byte (i2ccmd, data[i], true);
        //printf("%x ", data[i]);
    }
    //printf("\r\n");
    i2c_master_stop (i2ccmd);

    #ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "%s Sending :", __func__);
    esp_log_buffer_hex(TAG,data,count+7);
    #endif

    esp_err_t result = ESP_OK;
    result = i2c_master_cmd_begin (PN532_I2C_PORT, i2ccmd, I2C_WRITE_TIMEOUT / portTICK_PERIOD_MS);

    if (result != ESP_OK)
    {
        char *resultText = NULL;
        switch (result)
        {
        case ESP_ERR_INVALID_ARG:
        resultText = "Parameter error";
        break;
        case ESP_FAIL:
        resultText = "Sending command error, slave doesn’t ACK the transfer.";
        break;
        case ESP_ERR_INVALID_STATE:
        resultText = "I2C driver not installed or not in master mode.";
        break;
        case ESP_ERR_TIMEOUT:
        resultText = "Operation timeout because the bus is busy. ";
        break;
        }
        ESP_LOGE(TAG, "%s I2C write failed: %s", __func__, resultText);
        return ESP_FAIL;
    }

    i2c_cmd_link_delete (i2ccmd);
    return ESP_OK;
}

int PN532_I2C_ReadData(uint8_t* data, uint16_t count) {
    uint8_t status[] = {0x00};
    uint8_t frame[count + 1];
    i2c_read(status, sizeof(status));
    if (status[0] != PN532_I2C_READY) {
        return PN532_STATUS_ERROR;
    }
    i2c_read(frame, count + 1);
    for (uint8_t i = 0; i < count; i++) {
        data[i] = frame[i + 1];
    }
    return PN532_STATUS_OK;
}

int PN532_I2C_WriteData(uint8_t *data, uint16_t count) {
    i2c_write(data, count);
    return PN532_STATUS_OK;
}

bool PN532_I2C_WaitReady(uint32_t timeout) {
    uint8_t status[] = {0x00};
    uint32_t tickstart = esp_timer_get_time();
    while (esp_timer_get_time() - tickstart < timeout*1000) {
        
        i2c_read(status, sizeof(status));
        //ESP_LOGD(TAG, "Status req:%x", status[0]);
        if (status[0] == PN532_I2C_READY) {
            return true;
        } else {
            vTaskDelay (5 / portTICK_PERIOD_MS);
        }
    }
    return false;
}

int PN532_I2C_Wakeup(void) {
    // TODO
	/*
    HAL_GPIO_WritePin(PN532_REQ_GPIO_Port, PN532_REQ_Pin, GPIO_PIN_SET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(PN532_REQ_GPIO_Port, PN532_REQ_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(PN532_REQ_GPIO_Port, PN532_REQ_Pin, GPIO_PIN_SET);
    HAL_Delay(500);
    */
    return PN532_STATUS_OK;
}

int PN532_I2C_Init(PN532* pn532, uint8_t sda, uint8_t scl, i2c_port_t i2c_port_number){
    
    
    // init the pn532 functions
    //pn532->reset =  PN532_Reset;
    pn532->read_data = PN532_I2C_ReadData;
    pn532->write_data = PN532_I2C_WriteData;
    pn532->wait_ready = PN532_I2C_WaitReady;
    pn532->wakeup = PN532_I2C_Wakeup;
    pn532->log = PN532_Log;

    i2c_config_t conf;
    //Open the I2C Bus
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = sda;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
    //conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = scl;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
    //conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 100000;
    conf.clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL;

    if (i2c_param_config (PN532_I2C_PORT, &conf) != ESP_OK){
        ESP_LOGE(TAG, "i2c_param_config FAIL");
        return ESP_FAIL;
    }
    if (i2c_driver_install (PN532_I2C_PORT, conf.mode, 0, 0, 0) != ESP_OK){
        ESP_LOGE(TAG, "i2c_driver_install FAIL");
        return ESP_FAIL;
    }
    
    //Needed due to long wake up procedure on the first command on i2c bus. May be decreased
    if (i2c_set_timeout (PN532_I2C_PORT, 30) != ESP_OK){
        ESP_LOGE(TAG, "i2c_set_timeout FAIL");
        return ESP_FAIL;
    }
    

    // hardware wakeup
    pn532->wakeup();

    return ESP_OK;
}
/**************************************************************************
 * End: I2C
 **************************************************************************/
