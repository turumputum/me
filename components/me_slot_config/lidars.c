#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include "reporter.h"
#include "stateConfig.h"

#include "esp_log.h"
#include "me_slot_config.h"

extern uint8_t SLOTS_PIN_MAP[4][3];
extern configuration me_config;
extern stateStruct me_state;

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
static const char *TAG = "LIDARS";

void benewakeLidar_task(void *arg){
    int num_of_slot = *(int*) arg;
    if(num_of_slot>1){
        ESP_LOGD(TAG, "Wrong slot!!!");
        vTaskDelete(NULL);
    }

    uint8_t uart_num=UART_NUM_2;
    if(num_of_slot==0){
        uart_num = UART_NUM_2;
    }else if(num_of_slot==1){
        uart_num = UART_NUM_1;
    }
    #define BUF_SIZE 256
    uint8_t data[BUF_SIZE];
    size_t len;

    uint8_t tx_pin = SLOTS_PIN_MAP[num_of_slot][0];
    uint8_t rx_pin = SLOTS_PIN_MAP[num_of_slot][1];
    
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(uart_num, BUF_SIZE * 2, 0, 0, NULL,0);

    uint16_t deadBand=1;
    if (strstr(me_config.slot_options[num_of_slot], "dead_band")!=NULL){
		deadBand = get_option_int_val(num_of_slot, "dead_band");
		if(deadBand<=0){
			ESP_LOGD(TAG,"Lidar dead_band wrong format, set default slot:%d", deadBand);
			deadBand=1; //default val
		}else{
            ESP_LOGD(TAG,"Lidar set dead_band:%d for slot:%d", deadBand, num_of_slot);
        }
	}

    //ESP_LOGD(TAG,"Config ok lets listen");
    
    uint8_t rawByte[3];
    //uint8_t* rawByte = (uint8_t*) malloc(BUF_SIZE+1);
    uint8_t f_msg_start=0;
    uint8_t index=0;
    uint64_t  reportTick=esp_timer_get_time();
    uint16_t prev_dist=0;
    uint32_t msgTick = xTaskGetTickCount();

    while (1) {
        
        
        len = uart_read_bytes(uart_num, rawByte, 1, 5 / portTICK_RATE_MS);
        if (len > 0) {
            if(f_msg_start==1){
                data[index]=rawByte[0];
                index++;
                if(index==10){
                    uint8_t checksum = 0;
                    // Calculate checksum
                    for (int i = 0; i < 8; i++) {
                        checksum += data[i];
                    }

                    if(checksum == data[8]){
                        uint16_t dist = ((uint16_t)data[3]<< 8) | data[2];
                        uint16_t delta = abs(prev_dist-dist);
                        if ((dist<13000)&&(delta>=deadBand)){
                            //ESP_LOGD(TAG,"Lidar_%d distance is:%d",num_of_slot,dist);
                            ESP_LOGD(TAG,"Lidar_%d delta is:%d dead_band:%d",num_of_slot,delta,deadBand);
                            prev_dist=dist;
                            int str_len=strlen(me_config.device_name)+strlen("/lidar_")+8;
				            char *str = (char*)malloc(str_len * sizeof(char));
                            sprintf(str,"%s/lidar_%d:%d", me_config.device_name, num_of_slot, dist);
                            report(str);

                            msgTick = xTaskGetTickCount();
                        }
                        
                        vTaskDelay(pdMS_TO_TICKS(50));
                        uart_flush_input(uart_num);
                    }else{
                        //ESP_LOGD(TAG,"Checksum FAIL %d sum:%d data:%d",num_of_slot,checksum, data[8]);
                    }
                    index = 0;
                    f_msg_start=0;
                }

                
            }
            
            if(rawByte[0] == 0x59){
                if(index==0){
                    data[index]=rawByte[0];
                    index++;
                }else if(index==1){
                    data[index]=rawByte[0];
                    f_msg_start=1;
                    index++;
                }
            }

            if(abs(xTaskGetTickCount()-msgTick)>5000000){
                    ESP_LOGD(TAG,"Lidar fail! try  reset");
                    vTaskDelay(pdMS_TO_TICKS(100));
                    esp_restart();
                }
            

        }
    }
}

void start_benewakeLidar_task(int num_of_slot){
    uint32_t heapBefore = xPortGetFreeHeapSize();
    xTaskCreate(benewakeLidar_task, "benewakeLidar_task", 1024*4, &num_of_slot, 5, NULL);
    ESP_LOGD(TAG,"Lidar init ok: %d Heap usage: %d free heap:%d", num_of_slot, heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
}