#include <stdio.h>
#include <string.h>
#include "reporter.h"
#include "esp_log.h"
#include "myCDC.h"
#include "stateConfig.h"
#include "executor.h"
#include <tinyosc.h>
#include <sys/socket.h>
#include <netdb.h>
#include <lwip/sockets.h>
#include <netinet/in.h>

#include "myMqtt.h"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
static const char *TAG = "REPORTER";

#define MAILBOX_SIZE 10
#define MAX_STRING_LENGTH 100

typedef struct
{
	char str[MAX_STRING_LENGTH];
	size_t length;
} message_t;

QueueHandle_t mailbox;

extern configuration me_config;
extern stateStruct me_state;

void reporter_init()
{
}

// void cross_linker(char *msg) {
//	char source[strlen(msg)];
//	strcpy(source, msg);
//	char *rest;
//	char *trigger = source + strlen(me_config.device_name) + 1;
//	trigger = strtok_r(trigger, ":", &rest);
//	if (trigger == NULL) {
//		ESP_LOGD(TAG, "Link event wrong format: %s", msg);
//		return;
//	}
//	int payload = atoi(rest);
//	char *type = strtok_r(trigger, "_", &rest);
//	if (type == NULL) {
//		ESP_LOGD(TAG, "Link event wrong format: %s", msg);
//		return;
//	}
//	int slot_num = atoi(rest);
//
//	char crosslinks[strlen(me_config.slot_cross_link[slot_num])];
//	strcpy(crosslinks, me_config.slot_cross_link[slot_num]);
//	char *crosslink;
//	char *croslink_rest = crosslinks;
//	do {
//		crosslink = strtok_r(croslink_rest, ",", &croslink_rest);
//		if (crosslink != NULL) {
//			ESP_LOGD(TAG, "Cross_link:%s", crosslink);
//			char *trigger;
//			char *trigger_rest;
//			char *action;
//			char *action_rest;
//			trigger = strtok_r(crosslink, "=>", &action);
//			if (trigger == NULL) {
//				ESP_LOGD(TAG, "Link wrong format: %s", crosslink);
//				return;
//			}
//			trigger = strtok_r(trigger, ":", &trigger_rest);
//			if (trigger == NULL) {
//				ESP_LOGD(TAG, "Link wrong format: %s", crosslink);
//				return;
//			}
//			int trigger_val = atoi(trigger_rest);
//			action = strtok_r(action, ":", &action_rest);
//			if (action == NULL) {
//				ESP_LOGD(TAG, "Link wrong format: %s", crosslink);
//				return;
//			}
//			int action_val = atoi(action_rest);
//
//			ESP_LOGD(TAG, "Parse res: trigger=%s::%d, action=%s::%d", trigger,
//					trigger_val, action, action_val);
//		}
//	} while (crosslink != NULL);
// }

void crosslinker_task(void *parameter)
{
	// Wait for a message to arrive in the mailbox
	mailbox = xQueueCreate(MAILBOX_SIZE, sizeof(message_t));
	if (mailbox == NULL)
	{
		ESP_LOGE(TAG, "Mailbox create FAIL");
	}
	message_t received_message;
	while (1)
	{
		if (xQueueReceive(mailbox, &received_message, portMAX_DELAY) == pdPASS)
		{
			// ESP_LOGD(TAG, "Crosslinker incoming:%s", received_message.str);
			char *event = received_message.str + strlen(me_config.device_name) + 1;
			int slot_num;
			if (strstr(event, "player") != NULL)
			{
				slot_num = 0;
			}
			else
			{
				if (strstr(event, "_") != NULL)
				{
					char *num_index = strstr(event, "_") + 1;
					slot_num = num_index[0] - '0';
				}
				else
				{
					slot_num = -1;
				}
			}
			// ESP_LOGD(TAG, "event:%s, slot_num=%d", event, slot_num);

			if (slot_num >= 0)
			{
				char crosslinks[strlen(me_config.slot_cross_link[slot_num])];
				strcpy(crosslinks, me_config.slot_cross_link[slot_num]);
				char *crosslink = NULL;
				char *croslink_rest = crosslinks;
				uint8_t last_link = 0;
				do
				{
					if (strstr(croslink_rest, ",") != NULL)
					{
						crosslink = strtok_r(croslink_rest, ",", &croslink_rest);
					}
					else
					{
						crosslink = croslink_rest;
						last_link = 1;
					}
					//				else {
					//					ESP_LOGD(TAG, "Cross_links string END:");
					//					break;
					//				}
					if (crosslink != NULL)
					{
						// ESP_LOGD(TAG, "Cross_link:%s", crosslink);
						char *trigger;
						char *action;

						trigger = strtok_r(crosslink, "=>", &action);
						action = action + 1;

						if (strstr(trigger, event) != NULL)
						{
							ESP_LOGD(TAG, "Crosslink event:%s, trigger=%s, action=%s", event, trigger, action);
							char output_action[strlen(me_config.device_name) + strlen(action) + 2];
							sprintf(output_action, "%s/%s", me_config.device_name, action);
							execute(output_action);
						}
						else
						{
							// ESP_LOGD(TAG, "BAD event:%s, trigger=%s, action=%s", event, trigger, action);
						}
					}
					if (last_link == 1)
					{
						break;
					}

				} while (crosslink != NULL);
			}
		}
	}
}

void report(char *msg)
{
	//	char tmpStr[64];
	//	memset(tmpStr, 0,64);
	//	sprintf(tmpStr, "monofonMSD\n");
	ESP_LOGD(TAG, "%s", msg);
	usbprint(msg);
	if (me_state.MQTT_init_res == ESP_OK)
	{
		if (strstr(msg, ":") != NULL)
		{
			// ESP_LOGD(TAG, "Let's publish");
			char tmpString[strlen(msg)];
			strcpy(tmpString, msg);
			char *rest;
			char *tok = strtok_r(tmpString, ":", &rest);
			mqtt_pub(tok, rest);
		}
	}
	if((me_state.osc_socket >= 0))
	{
		char msg_copy[strlen(msg)];
		strcpy(msg_copy, msg);
		char tmpString[strlen(msg)+9];
		char *rest;
		char *tok = strtok_r(msg_copy, ":", &rest);
		int len=0;
		if(strstr(rest, ".")!=NULL){
			float tmp = atof(rest);
			len = tosc_writeMessage(tmpString, strlen(msg)+9, tok, "f", tmp);
			//ESP_LOGD(TAG, "OSC f string%s  rest:%f strlenMsg:%i", tmpString , tmp, strlen(msg));
		}else{
			int tmp = atoi(rest);
			len = tosc_writeMessage(tmpString, strlen(msg)+9, tok, "i", tmp);
			//ESP_LOGD(TAG, "OSC i string%s", tmpString );
		}
		

		//len = tosc_writeMessage(tmpString, strlen(tmpString), tok, "s", rest);
		// Create an OSC message
		struct sockaddr_in destAddr = {0};
		destAddr.sin_addr.s_addr = inet_addr(me_config.oscServerAdress);
		destAddr.sin_family = 2;
		destAddr.sin_port = htons(me_config.oscServerPort);

		int res = sendto(me_state.osc_socket, tmpString, len, 0, (struct sockaddr *)&destAddr, sizeof(destAddr));
		if (res < 0)
		{
			ESP_LOGE(TAG,"Failed to send osc errno: %d len:%d host:%d\n", errno, len, destAddr.sin_addr.s_addr);
			return;
		}
		else
		{
			ESP_LOGD(TAG,"send osc OK: \n");
		}
	}

	//---send crosslink message---
	message_t message;
	message.length = strlen(msg);
	strcpy(message.str, msg);
	if (xQueueSend(mailbox, &message, portMAX_DELAY) != pdPASS)
	{
		ESP_LOGE(TAG, "Send message FAIL");
	}
}

// void inbox_handler(char *msg){
//	char *tok = msg+ strlen(me_config.device_name)+1;
//	char *rest;
//	tok = strtok_r(tok,":", &rest);
//	int payload = atoi(rest);
//	char *type = strtok_r(tok, "_", &rest);
//	int slot_num = atoi(rest);
//	if(strcmp(type,"optorelay")==0){
//
//	}
//
// }
