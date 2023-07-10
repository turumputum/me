#include "me_slot_config.h"
#include <string.h>
#include <stdio.h>
#include "executor.h"
#include "stateConfig.h"
#include "include/buttons.h"
#include "esp_log.h"
#include "reporter.h"
#include "audioPlayer.h"
#include "3n_mosfet.h"
#include "encoders.h"
#include "lidars.h"

#include "myMqtt.h"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
static const char *TAG = "ME_SLOT_CONFIG";

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

extern configuration me_config;

uint8_t SLOTS_PIN_MAP[4][3] = {{4,5,10},{17,18,0},{3,41,0},{2,1,0}};

int init_slots(void){
	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();

	for(int i=0;i<8; i++){
		//ESP_LOGD(TAG,"check mode:%s",me_config.slot_mode[i]);
		if(!memcmp(me_config.slot_mode[i], "audio_player_mono", 17)){
			audioInit();
		}else if(!memcmp(me_config.slot_mode[i], "button_optorelay", 16)){
			start_button_task(i);
			init_optorelay(i);
		}else if(!memcmp(me_config.slot_mode[i], "button_led", 10)){
			start_button_task(i);
			init_led(i);
		}else if(!memcmp(me_config.slot_mode[i], "3n_mosfet", 9)){
			init_3n_mosfet(i);
		}else if(!memcmp(me_config.slot_mode[i], "encoderPWM", 10)){
			start_encoderPWM_task(i);
		}else if(!memcmp(me_config.slot_mode[i], "benewake_lidar", 10)){
			start_benewakeLidar_task(i);
		}
	}

	ESP_LOGD(TAG, "Load config complite. Duration: %d ms. Heap usage: %d",
				(xTaskGetTickCount() - startTick) * portTICK_RATE_MS,
				heapBefore - xPortGetFreeHeapSize());
	return ESP_OK;
}

int get_option_int_val(int num_of_slot, char* string){
	char *ind_of_vol = strstr(me_config.slot_options[num_of_slot], string);
	char options_copy[strlen(ind_of_vol)];
	strcpy(options_copy, ind_of_vol);
	char *rest;
	char *ind_of_eqal=strstr(ind_of_vol, ":");
	if(ind_of_eqal!=NULL){
		if(strstr(ind_of_vol, ",")!=NULL){
			ind_of_vol = strtok_r(options_copy,",",&rest);
		}
		return atoi(ind_of_eqal+1);
		ESP_LOGD(TAG, "Set volume:%d", me_config.volume);
	}else{
		return -1;
		ESP_LOGW(TAG, "Volume options wrong format:%s", ind_of_vol);
	}
}




