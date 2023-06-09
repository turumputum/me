#include <stdio.h>
#include "myMqtt.h"
#include "stateConfig.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "executor.h"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
static const char *TAG = "mqtt";

extern stateStruct me_state;
extern configuration me_config;

char phonUp_State_topic[100];
char lifeTime_topic[100];

esp_mqtt_client_handle_t client;

extern QueueHandle_t exec_mailbox;

void mqtt_pub(const char *topic, const char *string){
    int msg_id = esp_mqtt_client_publish(client, topic, string, 0, 0, 0);
    //ESP_LOGD(TAG, "sent publish successful, msg_id=%d", msg_id);
}

void mqtt_sub(const char *topic){
	esp_mqtt_client_subscribe(client, topic, 0);
	ESP_LOGD(TAG, "Subcribed successful, topic:%s", topic);
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    //ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    //esp_mqtt_client_handle_t client = event->client;
    //int msg_id;
	switch ((esp_mqtt_event_id_t) event_id) {
	case MQTT_EVENT_CONNECTED:
		ESP_LOGD(TAG, "MQTT_EVENT_CONNECTED");
		me_state.MQTT_init_res = ESP_OK;
		ESP_LOGD(TAG, "MQTT_CONNEKT_OK");
		for (int i = 0; i < me_state.action_topic_list_index; i++) {
			mqtt_sub(me_state.action_topic_list[i]);
		}
		//msg_id = esp_mqtt_client_subscribe(client, "phonState_topic", 0);
		//ESP_LOGD(TAG, "sent subscribe successful, msg_id=%d", msg_id);
		break;
	case MQTT_EVENT_DISCONNECTED:
		ESP_LOGD(TAG, "MQTT_EVENT_DISCONNECTED");
		break;

	case MQTT_EVENT_SUBSCRIBED:
		ESP_LOGD(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_UNSUBSCRIBED:
		ESP_LOGD(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_PUBLISHED:
		ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_DATA:
		ESP_LOGI(TAG, "MQTT_EVENT_DATA");

		//uint8_t len = event->topic_len + event->data_len + 3;
		//char str[len];
//		sprintf(tmpStr, "%s:%s", event->topic, event->data);
		exec_message_t message;
		message.length = sprintf(message.str, "%.*s:%.*s", event->topic_len, event->topic, event->data_len, event->data);
		ESP_LOGD(TAG, "Add to exec_queue:%s ",message.str);
		//message.length = strlen(tmpStr);
		//strcpy(message.str, tmpStr);
		if (xQueueSend(exec_mailbox, &message, portMAX_DELAY) != pdPASS) {
			ESP_LOGE(TAG, "Send message FAIL");
        }
//		printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
//		printf("DATA=%.*s\r\n", event->data_len, event->data);
		break;
	case MQTT_EVENT_ERROR:
		ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
		if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
			log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
			log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
			log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
			ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

		}
		break;
	default:
		//ESP_LOGI(TAG, "Other event id:%d", event->event_id);
		break;
	}
}

int mqtt_app_start(void)
{
    uint32_t startTick = xTaskGetTickCount();
    uint32_t heapBefore = xPortGetFreeHeapSize();

    if(me_config.mqttBrokerAdress[0]==0){
        ESP_LOGW(TAG, "Empty mqtt broker adress, mqtt disable");
        return -1;
    }

	

    esp_mqtt_client_config_t mqtt_cfg = {
        //.uri = "mqtt://192.168.1.60:1883",
		//.uri = "mqtt://192.168.1.60:1883",
    	//.client_id = me_config.device_name,
        .host = me_config.mqttBrokerAdress,
		//TODO write will msg
		//.lwt_topic
		//.lwt_msg
    };

	//ESP_LOGD(TAG, "Broker addr:%s", mqtt_cfg.uri);

    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));

    ESP_LOGD(TAG, "MQTT init complite. Duration: %d ms. Heap usage: %d", (xTaskGetTickCount() - startTick) * portTICK_RATE_MS, heapBefore - xPortGetFreeHeapSize());

    return ESP_OK;
}





