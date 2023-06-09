#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "me_slot_config.h"

#include "executor.h"
#include "stateConfig.h"
#include "esp_log.h"

#include "3n_mosfet.h"

#include "audioPlayer.h"

extern uint8_t SLOTS_PIN_MAP[4][3];
extern configuration me_config;
extern stateStruct me_state;

exec_message_t exec_message;
QueueHandle_t exec_mailbox;

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
static const char *TAG = "EXECUTOR";

void init_optorelay(int slot_num) {
	uint32_t heapBefore = xPortGetFreeHeapSize();
	//---init hardware---
	uint8_t pin_num = SLOTS_PIN_MAP[slot_num][1];
	//printf("slot:%d pin:%d \r\n", slot_num, pin_num);
	gpio_pad_select_gpio(pin_num);
	gpio_set_direction(pin_num, GPIO_MODE_OUTPUT);

	//---set default state---
	int optorelay_inverse = 0;
	if (strstr(me_config.slot_options[slot_num], "optorelay_inverse") != NULL) {
		optorelay_inverse = 1;
	}

	uint8_t def_state = optorelay_inverse;
	if (strstr(me_config.slot_options[slot_num], "optorelay_default_high") != NULL) {
		if (strstr(me_config.slot_options[slot_num], "optorelay_inverse") != NULL) {
		} else {
			def_state = !def_state;
		}
	}
	ESP_ERROR_CHECK(gpio_set_level(pin_num, (uint32_t )def_state));

	//---add action to topic list---
	char *str = calloc(strlen(me_config.device_name) + 16, sizeof(char));
	sprintf(str, "%s/optorelay_%d", me_config.device_name, slot_num);
	me_state.action_topic_list[me_state.action_topic_list_index] = str;
	me_state.action_topic_list_index++;

	ESP_LOGD(TAG, "Optorelay inited for slot: %d Heap usage: %d free heap:%d", slot_num, heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
}

void init_led(int slot_num) {
	uint32_t heapBefore = xPortGetFreeHeapSize();
	//---init hardware---
	uint8_t pin_num = SLOTS_PIN_MAP[slot_num][1];
	//printf("slot:%d pin:%d \r\n", slot_num, pin_num);
	gpio_pad_select_gpio(pin_num);
	gpio_set_direction(pin_num, GPIO_MODE_OUTPUT);

	//---set default state---
	int optorelay_inverse = 0;
	if (strstr(me_config.slot_options[slot_num], "led_inverse") != NULL) {
		optorelay_inverse = 1;
	}

	uint8_t def_state = optorelay_inverse;
	if (strstr(me_config.slot_options[slot_num], "led_default_high") != NULL) {
		if (strstr(me_config.slot_options[slot_num], "led_inverse") != NULL) {
		} else {
			def_state = !def_state;
		}
	}
	ESP_ERROR_CHECK(gpio_set_level(pin_num, (uint32_t )def_state));

	//---add action to topic list---
	char *str = calloc(strlen(me_config.device_name) + 16, sizeof(char));
	sprintf(str, "%s/led_%d", me_config.device_name, slot_num);
	me_state.action_topic_list[me_state.action_topic_list_index] = str;
	me_state.action_topic_list_index++;

	ESP_LOGD(TAG, "Led inited for slot: %d Heap usage: %d free heap:%d", slot_num, heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
}

void exec_optorelay(int slot_num, int payload) {
	int optorelay_inverse = 0;
	uint8_t pin_num = SLOTS_PIN_MAP[slot_num][1];
	if (strstr(me_config.slot_options[slot_num], "optorelay_inverse") != NULL) {
		optorelay_inverse = 1;
	}
	int level = optorelay_inverse ? !payload : payload;
	gpio_set_level(pin_num, level);
	//printf("pin_num:%d\n", pin_num);
	ESP_LOGD(TAG, "Optorelay set:%d for slot:%d inverse:%d level:%d", payload, slot_num, optorelay_inverse, level);
}

typedef struct {
	uint8_t pin;
	int interval;
} FlashArgs_t;

void flash_led_task(void *pvParameters) {

	uint8_t slot_num = *((uint8_t*) pvParameters);
	uint8_t pin_num = SLOTS_PIN_MAP[slot_num][1];
	int flash_interval;
	char *rest;
	char *tok = strstr(me_config.slot_options[slot_num], "flash");
	if (strstr(me_config.slot_options[slot_num], ",") != NULL) {
		tok = strtok_r(tok, ",", &rest);
	}
	//ESP_LOGD(TAG, "---option is:%s", tok);
	if (strstr(tok, ":") != NULL) {
		tok = strstr(tok, ":") + 1;
		//ESP_LOGD(TAG, "---value is:%s", tok);
		flash_interval = atoi(tok);
	}else{
		flash_interval = 500;
	}

	ESP_LOGD(TAG, "Start flash task for slot:%d interval:%d", slot_num, flash_interval);

	int level = 0;
	while (1) {
		ESP_LOGD(TAG, "task work, level:%d", level);
		level = !level;

		gpio_set_level(pin_num, level);
		if(level==0){
			vTaskDelay(pdMS_TO_TICKS(flash_interval*2));
		}else{
			vTaskDelay(pdMS_TO_TICKS(flash_interval));
		}
	}
}

void exec_led(int slot_num, int payload) {
	int led_inverse = 0;

	if (strstr(me_config.slot_options[slot_num], "led_inverse") != NULL) {
		led_inverse = 1;
	}

	//ESP_LOGD(TAG, "Led option string:%s", me_config.slot_options[slot_num]);
	if (strstr(me_config.slot_options[slot_num], "flash") != NULL) {
		if (payload == 1) {

			if (me_state.slot_task[slot_num] == NULL) {
				//ESP_LOGD(TAG, "Led start flash task with interval:%d", flash_interval);
				xTaskCreate(flash_led_task, "", 1024 * 3, &slot_num, 6, &me_state.slot_task[slot_num]);
			}else{
				eTaskState taskState = eTaskGetState(me_state.slot_task[slot_num]);
				//ESP_LOGD(TAG, "---beforeStart taskState:%d", taskState);
				if(taskState==eDeleted){
					xTaskCreate(flash_led_task, "", 1024 * 3, &slot_num, 6, &me_state.slot_task[slot_num]);
				}else{
					ESP_LOGD(TAG, "Task is running");
				}
			}

		} else if (payload == 0) {
			if (me_state.slot_task[slot_num] != NULL) {
				eTaskState taskState = eTaskGetState(me_state.slot_task[slot_num]);
				ESP_LOGD(TAG, "---beforeStop taskState:%d", taskState);
				if(taskState!=eDeleted){
					uint8_t pin_num = SLOTS_PIN_MAP[slot_num][1];
					vTaskDelete(me_state.slot_task[slot_num]);
					gpio_set_level(pin_num, 0);
					ESP_LOGD(TAG, "Led flash task STOP");
				}else{
					ESP_LOGD(TAG, "Task is stopped");
				}
			}
		}

	} else {
		uint8_t pin_num = SLOTS_PIN_MAP[slot_num][1];
		int level = led_inverse ? !payload : payload;
		gpio_set_level(pin_num, level);
		//printf("pin_num:%d\n", pin_num);
		ESP_LOGD(TAG, "Led set:%d for slot:%d inverse:%d level:%d", payload, slot_num, led_inverse, level);
	}


}

void execute(char *action) {
ESP_LOGD(TAG, "Execute action:%s", action);
char source[strlen(action)];
strcpy(source, action);
char *rest;
char *tok = source + strlen(me_config.device_name) + 1;
if (strstr(tok, ":") == NULL) {
	ESP_LOGW(TAG, "Action wrong format: %s", action);
	return;
}
tok = strtok_r(tok, ":", &rest);

if (strcmp(tok, "play_track") == 0) {
	audioPlay(rest);
} else if (strcmp(tok, "player_stop") == 0) {
	audioStop();
} else if (strcmp(tok, "player_pause") == 0) {
	audioPause();
} else if (strcmp(tok, "set_volume") == 0) {
	setVolume_str(rest);
} else {
	char *payload = rest;
//ESP_LOGD(TAG,"tok:%s",tok);
	// if (strstr(tok, "_") == NULL) {
	// 	ESP_LOGW(TAG, "Unknown action or wrong format: %s", action);
	// 	return;
	// }
	//char *type = strtok_r(tok, "_", &rest);
	char *type = tok;
//ESP_LOGD(TAG,"rest:%s",rest);
	int slot_num = atoi(rest);
	if (strcmp(type, "optorelay") == 0) {
		int val = atoi(payload);
		exec_optorelay(slot_num, val);
	} else if (strcmp(type, "led") == 0) {
		int val = atoi(payload);
		exec_led(slot_num, val);
	} else if (strcmp(type, "set") == 0) {
		setRGB(slot_num, payload);
	} else if (strcmp(type, "glitch") == 0) {
		setGlitch(slot_num, payload);
	} else {
		ESP_LOGW(TAG, "Unknown action type: %s", action);
	}
}

//printf("type:%s stot_num:%d payload:%d \n", type, slot_num, payload);

//strtok_r(topic_no_devName, "\n", &rest);
}
