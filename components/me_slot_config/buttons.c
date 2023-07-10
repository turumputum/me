#include "buttons.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"


#include "reporter.h"
#include "stateConfig.h"

#include "esp_log.h"
#include "me_slot_config.h"

extern uint8_t SLOTS_PIN_MAP[4][3];
extern configuration me_config;
extern stateStruct me_state;

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
static const char *TAG = "BUTTONS";

void button_task(void *arg){
#define THRESHOLD 4
	int num_of_slot = *(int*) arg;
	uint8_t pin_num = SLOTS_PIN_MAP[num_of_slot][0];
	gpio_reset_pin(pin_num);
	gpio_set_direction(pin_num, GPIO_MODE_INPUT);
	int button_state=0;
	int prev_state=0;

	int button_inverse=0;
	if (strstr(me_config.slot_options[num_of_slot], "button_inverse")!=NULL){
		button_inverse=1;
	}

	int  count=0;
	while(1){
		vTaskDelay(pdMS_TO_TICKS(10));
		if(gpio_get_level(pin_num)){
			count++;
			if(count>THRESHOLD*2){count = THRESHOLD*2;}
		}else{
			count--;
			if(count<0){count = 0;}
		}
		if(count>THRESHOLD){
			button_state=button_inverse ? 0 : 1;
		}else{
			button_state=button_inverse ? 1 : 0;
		}
		if(button_state != prev_state){
			prev_state = button_state;

			ESP_LOGD(TAG,"button_%d:%d inverse:%d", num_of_slot, button_state, button_inverse );

			int str_len=strlen(me_config.device_name)+strlen("/button_")+4;
			char *str = (char*)malloc(str_len * sizeof(char));
			sprintf(str,"%s/button_%d:%d", me_config.device_name, num_of_slot, button_state);
			report(str);
			free(str);

		}
	}
}

void start_button_task(int num_of_slot){
	uint32_t heapBefore = xPortGetFreeHeapSize();
	int t_slot_num = num_of_slot;
	char tmpString[60];
	sprintf(tmpString, "task_button_%d", num_of_slot);
	xTaskCreate(button_task, tmpString, 1024*2, &t_slot_num,12, NULL);

	char *str = calloc(strlen(me_config.device_name)+10, sizeof(char));
	sprintf(str, "%s/button_%d",me_config.device_name, num_of_slot);
	me_state.triggers_topic_list[me_state.triggers_topic_list_index]=str;
	me_state.triggers_topic_list_index++;

	ESP_LOGD(TAG,"Button task created for slot: %d Heap usage: %d free heap:%d", num_of_slot, heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
}
