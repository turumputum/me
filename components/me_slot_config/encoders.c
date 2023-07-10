#include "buttons.h"
#include "sdkconfig.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/projdefs.h"
#include "freertos/portmacro.h"
#include <driver/mcpwm.h>

#include "reporter.h"
#include "stateConfig.h"
#include "rotary_encoder.h"

#include "esp_system.h"
#include "esp_log.h"
#include "me_slot_config.h"

extern uint8_t SLOTS_PIN_MAP[4][3];
extern configuration me_config;
extern stateStruct me_state;
extern uint8_t led_segment;

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
static const char *TAG = "ENCODERS";
extern char debugString[200];
// uint64_t rTime, fTime;
// uint64_t dTime=0;
// uint8_t flag_calc;

typedef struct
{
	uint8_t flag;
	int64_t tick_rise;
	int64_t tick_fall;
	int64_t dTime;
} pwmEvent_t;

static void IRAM_ATTR rise_handler(void *args)
{
	pwmEvent_t *tickVals = (pwmEvent_t *)args;
	tickVals->tick_rise = esp_timer_get_time();
}

static void IRAM_ATTR fall_handler(void *args)
{
	pwmEvent_t *tickVals = (pwmEvent_t *)args;

	tickVals->tick_fall = esp_timer_get_time();

	// if((abs((tickVals->tick_fall-tickVals->tick_rise)-tickVals->dTime)>15)&&((tickVals->tick_fall-tickVals->tick_rise)>2)){
	if ((tickVals->tick_fall - tickVals->tick_rise) > 2)
	{
		tickVals->flag = 1;
		tickVals->dTime = (tickVals->tick_fall - tickVals->tick_rise);
	}
}

void encoderPWM_task(void *arg)
{
	int num_of_slot = *(int *)arg;

	uint8_t rise_pin_num = SLOTS_PIN_MAP[num_of_slot][0];
	gpio_pad_select_gpio(rise_pin_num);
	gpio_set_direction(rise_pin_num, GPIO_MODE_INPUT);
	gpio_pulldown_en(rise_pin_num);
	gpio_pullup_dis(rise_pin_num);
	gpio_set_intr_type(rise_pin_num, GPIO_INTR_POSEDGE);

	uint8_t fall_pin_num = SLOTS_PIN_MAP[num_of_slot][1];
	gpio_pad_select_gpio(fall_pin_num);
	gpio_set_direction(fall_pin_num, GPIO_MODE_INPUT);
	gpio_pulldown_en(fall_pin_num);
	gpio_pullup_dis(fall_pin_num);
	gpio_set_intr_type(fall_pin_num, GPIO_INTR_NEGEDGE);

	pwmEvent_t tickVals;
	gpio_install_isr_service(0);
	gpio_isr_handler_add(rise_pin_num, rise_handler, (void *)&tickVals);
	gpio_isr_handler_add(fall_pin_num, fall_handler, (void *)&tickVals);

#define INCREMENTAL 0
#define ABSOLUTE 1
	uint8_t encoderMode = INCREMENTAL;
	if (strstr(me_config.slot_options[num_of_slot], "absolute") != NULL)
	{
		encoderMode = ABSOLUTE;
		ESP_LOGD(TAG, "pwmEncoder mode: absolute slot:%d", num_of_slot);
	}
	else
	{
		ESP_LOGD(TAG, "pwmEncoder mode: incremental slot:%d", num_of_slot);
	}

#define MIN_VAL 3
#define MAX_VAL 926
	int pole = MAX_VAL - MIN_VAL;
	int num_of_pos;
	if (strstr(me_config.slot_options[num_of_slot], "num_of_pos") != NULL)
	{
		num_of_pos = get_option_int_val(num_of_slot, "num_of_pos");
		if (num_of_pos <= 0)
		{
			ESP_LOGD(TAG, "pwmEncoder num_of_pos wrong format, set default slot:%d", num_of_slot);
			num_of_pos = 24; // default val
		}
	}
	else
	{
		num_of_pos = 24; // default val
	}
	ESP_LOGD(TAG, "pwmEncoder num_of_pos:%d slot:%d", num_of_pos, num_of_slot);
	int offset;
	int pos_length = pole / num_of_pos;
	int64_t raw_val, filtred_val;
	int current_pos, prev_pos = -1;

	while (tickVals.flag != 1)
	{
		// vTaskDelay(pdMS_TO_TICKS(1)); / portTICK_PERIOD_MS
		vTaskDelay(1 / portTICK_PERIOD_MS);
	}
	raw_val = tickVals.dTime;

	current_pos = raw_val / pos_length;
	offset = (raw_val % pos_length) + (pos_length / 2); //
	ESP_LOGD(TAG, "pwmEncoder first_val:%lld offset:%d pos_legth:%d", raw_val, offset, pos_length);

	#define ANTI_DEBOUNCE_INERATIONS 5
	int anti_deb_mass_index = 0;
	int val_mass[ANTI_DEBOUNCE_INERATIONS];

	while (1)
	{
		vTaskDelay(pdMS_TO_TICKS(10));
		if (tickVals.flag)
		{
			raw_val = tickVals.dTime + offset;

			val_mass[anti_deb_mass_index] = raw_val;
			anti_deb_mass_index++;
			if (anti_deb_mass_index == ANTI_DEBOUNCE_INERATIONS)
			{
				anti_deb_mass_index = 0;
			}
			int sum = 0;
			for (int i = 1; i < ANTI_DEBOUNCE_INERATIONS; i++)
			{
				if (abs(val_mass[i] - val_mass[i - 1]) < 3)
				{
					sum++;
				}
			}
			if (sum >= (ANTI_DEBOUNCE_INERATIONS - 1))
			{
				current_pos = raw_val / pos_length - 1;
				if (current_pos >= num_of_pos)
				{
					current_pos = 0;
				}
			}

			if (current_pos != prev_pos)
			{
				// printf("val_mas: %d %d %d %d %d \r\n",val_mass[0],val_mass[1],val_mass[2],val_mass[3],val_mass[4]);
				//printf("Current_pos: %d  raw_val:%d sum_val:%d\r\n", current_pos, (int)raw_val, sum);
				int str_len = strlen(me_config.device_name) + strlen("/encoder_") + 8;
				char *str = (char *)malloc(str_len * sizeof(char));
				char dir[20];
				if (encoderMode == ABSOLUTE)
				{
					sprintf(str, "%s/encoder_%d:%d", me_config.device_name, num_of_slot, current_pos);
				}
				else
				{
					int delta = abs(current_pos - prev_pos);
					if (delta < (num_of_pos / 2))
					{
						if (current_pos < prev_pos)
						{
							sprintf(dir, "+%d", delta);
						}
						else
						{
							sprintf(dir, "-%d", delta);
						}
					}
					else
					{
						delta = num_of_pos - delta;
						if (current_pos < prev_pos)
						{
							sprintf(dir, "-%d", delta);
						}
						else
						{
							sprintf(dir, "+%d", delta);
						}
					}

					sprintf(str, "%s/encoder_%d:%s", me_config.device_name, num_of_slot, dir);
				}
				report(str);
				free(str);
				prev_pos = current_pos;
			}
			tickVals.flag = 0;
		}
	}
}

void start_encoderPWM_task(int num_of_slot)
{

	uint32_t heapBefore = xPortGetFreeHeapSize();
	int t_slot_num = num_of_slot;
	// int num_of_slot = *(int*) arg;
	xTaskCreate(encoderPWM_task, "encoderCalc", 1024 * 4, &t_slot_num, 1, NULL);
	// printf("----------getTime:%lld\r\n", esp_timer_get_time());

	ESP_LOGD(TAG, "pwmEncoder init ok: %d Heap usage: %d free heap:%d", num_of_slot, heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
}

//-------------------incremental encoder secttion--------------------------
void encoder_inc_task(void *arg)
{
	int num_of_slot = *(int *)arg;
	uint8_t a_pin_num = SLOTS_PIN_MAP[num_of_slot][0];
	uint8_t b_pin_num = SLOTS_PIN_MAP[num_of_slot][1];
	uint8_t zero_sens_pin = SLOTS_PIN_MAP[1][0];
	// ESP_ERROR_CHECK(gpio_install_isr_service(0));
	gpio_install_isr_service(0);
	rotary_encoder_info_t info = {0};
	ESP_ERROR_CHECK(rotary_encoder_init(&info, a_pin_num, b_pin_num, zero_sens_pin));
	//ESP_ERROR_CHECK(rotary_encoder_enable_half_steps(&info, 1));
	QueueHandle_t event_queue = rotary_encoder_create_queue();
	ESP_ERROR_CHECK(rotary_encoder_set_queue(&info, event_queue));

	// int16_t zero_sens_slot = -1;
	// //int8_t zero_sens_pin = -1;
	// uint8_t zero_sens_state, zero_sens_prev_state = gpio_get_level(zero_sens_pin);
	// int16_t disk_length = 2000;
	// int16_t sensor_length = -1;
	// int16_t sensor_begin = -1, sensor_end = -1;
	// int16_t offset = -1;
	int16_t pos, prev_pos=0;
	// if (strstr(me_config.slot_options[num_of_slot], "zero_sens_slot") != NULL)
	// {
	// 	zero_sens_slot = get_option_int_val(num_of_slot, "zero_sens_slot");
	// 	if (zero_sens_slot < 0)
	// 	{
	// 		ESP_LOGD(TAG, "Encoder zero_sens_slot wrong format");
	// 	}
	// 	else
	// 	{
	// 		ESP_LOGD(TAG, "Encoder set zero_sens_slot:%d for slot:%d", zero_sens_slot, num_of_slot);
	// 	}
	// 	zero_sens_pin = SLOTS_PIN_MAP[zero_sens_slot][0];
	// 	gpio_reset_pin(zero_sens_pin);
	// 	gpio_set_direction(zero_sens_pin, GPIO_MODE_INPUT);
	// }

	
	// gpio_reset_pin(zero_sens_pin);
	// gpio_set_direction(zero_sens_pin, GPIO_MODE_INPUT);


	//int16_t flag_disk_length=2;
	while (1)
	{
		// Wait for incoming events on the event queue.
		
		rotary_encoder_event_t event = {0};
		if (xQueueReceive(event_queue, &event, 10 / portTICK_PERIOD_MS) == pdTRUE)
		{	
			//ESP_LOGI(TAG, "Event: position %d, direction %s", event.state.position, event.state.direction ? (event.state.direction == ROTARY_ENCODER_DIRECTION_CLOCKWISE ? "CW" : "CCW") : "NOT_SET");
			
			pos = (event.state.position*(0.22));
			led_segment = event.state.position/205+1;
			if(led_segment>=8){
				led_segment=0;
			}
			if(pos!=prev_pos){
				//ESP_LOGD(TAG, "Position %d %s",event.state.position, debugString);
				//ESP_LOGD(TAG, "report pos:%d",  pos);
				int str_len=strlen(me_config.device_name)+strlen("/encoder_")+8;
				char *str = (char*)malloc(str_len * sizeof(char));
				sprintf(str,"%s/encoder_%d:%d", me_config.device_name, 0, pos);
				report(str);
				free(str);
			}
			// if(flag_disk_length<2){
			// 	//calc disk length
			// 	zero_sens_state = gpio_get_level(zero_sens_pin);
			// 	//ESP_LOGI(TAG, "zero_sens_state:%d",  zero_sens_state);
			// 	if (zero_sens_state != zero_sens_prev_state){
			// 		if ((zero_sens_state == 1) && (zero_sens_prev_state == 0)){
			// 			if(flag_disk_length==0){
			// 				disk_length=pos;
			// 				flag_disk_length=1;
			// 				ESP_LOGI(TAG, "Start calck disk_length:%d flag:%d", disk_length, flag_disk_length);
			// 			}else if((flag_disk_length==1)){
			// 				disk_length=abs(pos-disk_length);
			// 				ESP_LOGI(TAG, "Set disk_length:%d", disk_length);
			// 				flag_disk_length=2;
			// 			}
			// 		}
			// 		zero_sens_prev_state = zero_sens_state;
			// 	}
			// }else{
			// 	//pos to positive val
			// 	if(pos>=0){
			// 		while(pos>disk_length){
			// 			pos-=disk_length;
			// 		}
			// 	}else{
			// 		while(pos<0){
			// 			pos+=disk_length;
			// 		}
			// 	}

			// 	zero_sens_state = gpio_get_level(zero_sens_pin);
			// 	//ESP_LOGI(TAG, "zero_sens_state:%d",  zero_sens_state);
			// 	if (zero_sens_state != zero_sens_prev_state)
			// 	{
			// 		if ((zero_sens_state == 1) && (zero_sens_prev_state == 0))
			// 		{
			// 			sensor_begin = pos;
			// 			ESP_LOGI(TAG, "Set sensor_begin:%d", sensor_begin);
			// 		}
			// 		else if ((zero_sens_state == 0) && (zero_sens_prev_state == 1))
			// 		{
			// 			sensor_end = pos;
			// 			ESP_LOGI(TAG, "Set sensor_end:%d", sensor_end);
			// 		}
			// 		if ((sensor_begin > 0) && (sensor_end > 0))
			// 		{
			// 			sensor_length = abs(sensor_end - sensor_begin);
			// 			ESP_LOGI(TAG, "Set zero sensor length:%d", sensor_length);
			// 		}
			// 		zero_sens_prev_state = zero_sens_state;
			// 	}


			// }

			

			// // check zero sens

				
			
		}


		// if(pos>=0){
		// 	while(pos>353){
		// 		pos-=353;
		// 	}
		// }else{
		// 	while(pos<0){
		// 		pos+=353;
		// 	}
		// }
		// ESP_LOGI(TAG, "Event: position %d, direction %s", pos,
		//          event.state.direction ? (event.state.direction == ROTARY_ENCODER_DIRECTION_CLOCKWISE ? "CW" : "CCW") : "NOT_SET");
		// 	bt_state=gpio_get_level(bt_pin_num);

		// 	int str_len=strlen(me_config.device_name)+strlen("/encoder_")+8;
		// 	char *str = (char*)malloc(str_len * sizeof(char));
		// 	sprintf(str,"%s/encoder_%d:%d", me_config.device_name, num_of_slot, (int)(pos*1.0199));
		// 	report(str);
		// 	free(str);

		// 	//ESP_LOGI(TAG, "bt_state:%d bt_prev_state:%d", bt_state,bt_prev_state);
		// 	if (bt_state!=bt_prev_state){
		// 		if(bt_state==1){

		// 			if(event.state.direction==ROTARY_ENCODER_DIRECTION_CLOCKWISE){
		// 				offset=event.state.position;
		// 			}else{
		// 				offset=event.state.position-10;
		// 			}
		// 			ESP_LOGI(TAG, "Lets set ofset:%d", offset );
		// 		}
		// 		bt_prev_state = bt_state;
		// 	}
	}
}


void start_encoder_inc_task(int num_of_slot)
{
	uint32_t heapBefore = xPortGetFreeHeapSize();
	int t_slot_num = num_of_slot;
	// int num_of_slot = *(int*) arg;
	xTaskCreate(encoder_inc_task, "encoder_inc_task", 1024 * 4, &t_slot_num, 1, NULL);
	// printf("----------getTime:%lld\r\n", esp_timer_get_time());

	ESP_LOGD(TAG, "encoder_inc_task init ok: %d Heap usage: %d free heap:%d", num_of_slot, heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
}