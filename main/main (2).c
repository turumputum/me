/* Control with a touch pad playing MP3 files from SD Card

 This example code is in the Public Domain (or CC0 licensed, at your option.)

 Unless required by applicable law or agreed to in writing, this
 software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 CONDITIONS OF ANY KIND, either express or implied.
 */
#include <sd_card.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "periph_touch.h"
#include "periph_button.h"
#include "input_key_service.h"
#include "periph_adc_button.h"
#include "board.h"

#include "esp_vfs_fat.h"
#include "esp_spiffs.h"
// #include "sdmmc_cmd.h"
// #include "driver/sdmmc_host.h"
// #include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "sdcard_list.h"
#include "sdcard_scan.h"

#include "driver/adc.h"
#include "esp_adc_cal.h"

#include "leds.h"

#include "stateConfig.h"

#include "audioPlayer.h"

#include "LAN.h"
#include "mdns.h"

#include "ftp.h"

#include "lwip/dns.h"

#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "esp_vfs_cdcacm.h"
#include "myMqtt.h"
#include "tusb.h"
#include "me_slot_config.h"
#include "executor.h"
#include "reporter.h"
#include "encoders.h"
#include "rfid.h"

#include "myCDC.h"

#include "p9813.h"

extern uint8_t SLOTS_PIN_MAP[4][3];

extern void board_init(void);

#define USBD_STACK_SIZE 4096

#define MDNS_ENABLE_DEBUG 0

static const char *TAG = "MAIN";

#define SPI_DMA_CHAN 1
// #define MOUNT_POINT "/sdcard"
// static const char *MOUNT_POINT = "/sdcard";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#define CONFIG_FREERTOS_HZ 1000

int FTP_TASK_FINISH_BIT = BIT2;
EventGroupHandle_t xEventTask;

extern uint8_t FTP_SESSION_START_FLAG;
extern uint8_t FLAG_PC_AVAILEBLE;
extern uint8_t FLAG_PC_EJECT;

extern void usb_device_task(void *param);

RTC_NOINIT_ATTR int RTC_flagMscEnabled;

extern exec_message_t exec_message;
extern QueueHandle_t exec_mailbox;

uint8_t flag_ccw, flag_cw;

uint8_t led_segment = 0;
uint8_t prev_led_segment;

// static task
StackType_t usb_device_stack[USBD_STACK_SIZE];
StaticTask_t usb_device_taskdef;
// static task for cdc
// #define CDC_STACK_SZIE      (configMINIMAL_STACK_SIZE*3)
// StackType_t cdc_stack[CDC_STACK_SZIE];
// StaticTask_t cdc_taskdef;
// void usb_device_task(void *param);
// void cdc_task(void *params);

// int isMscEnabled() {
//	return flagMscEnabled;
// }

#define RTC_FLAG_DISABLED_VALUE 0xAAAA5555

configuration me_config;
stateStruct me_state;

uint32_t ADC_AVERAGE;

#define RELAY_1_GPIO GPIO_NUM_18
#define RELAY_2_GPIO GPIO_NUM_48

void listenListener(void *pvParameters);

void ftp_task(void *pvParameters);

void nvs_init()
{
	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();
	esp_err_t ret;
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	ESP_LOGD(TAG, "NVS init complite. Duration: %d ms. Heap usage: %d free heap:%d", (xTaskGetTickCount() - startTick) * portTICK_RATE_MS, heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
}

void spiffs_init()
{

	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();
	esp_err_t ret;
	esp_vfs_spiffs_conf_t conf = {.base_path = "/spiffs", .partition_label = NULL, .max_files = 10, .format_if_mount_failed = true};
	ret = esp_vfs_spiffs_register(&conf);
	size_t total = 0, used = 0;
	ret = esp_spiffs_info(NULL, &total, &used);

	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
	}
	else
	{
		ESP_LOGD(TAG, "SPIFFS init complite. Duration: %d ms. Heap usage: %d free heap:%d", (xTaskGetTickCount() - startTick) * portTICK_RATE_MS, heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
		ESP_LOGD(TAG, "Partition size: total: %d, used: %d", total, used);
	}
}

void mdns_start(void)
{
	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();
	char mdnsName[80];

	// set mDNS hostname (required if you want to advertise services)
	if (strlen(me_config.device_name) == 0)
	{
		sprintf(mdnsName, "%s", (char *)me_config.ssidT);
		strcpy(mdnsName, me_config.ssidT);
		ESP_LOGD(TAG, "Set mdns name: %s  device_name len:%d ", mdnsName, strlen(me_config.device_name));
	}
	else
	{
		sprintf(mdnsName, "%s", me_config.device_name);
		ESP_LOGD(TAG, "Set mdns name: %s  device_name len:%d ", mdnsName, strlen(me_config.device_name));
	}

	ESP_ERROR_CHECK(mdns_init());
	ESP_ERROR_CHECK(mdns_hostname_set(mdnsName));
	ESP_ERROR_CHECK(mdns_instance_name_set("monofon-instance"));
	mdns_service_add(NULL, "_ftp", "_tcp", 21, NULL, 0);
	mdns_service_instance_name_set("_ftp", "_tcp", "Monofon FTP server");
	strcat(mdnsName, ".local");
	ESP_LOGI(TAG, "mdns hostname set to: %s", mdnsName);
	mdns_txt_item_t serviceTxtData[1] = {
		{"URL", strdup(mdnsName)},
	};
	// sprintf()
	mdns_service_txt_set("_ftp", "_tcp", serviceTxtData, 1);

	ESP_LOGD(TAG, "mdns_start complite. Duration: %d ms. Heap usage: %d free heap:%d", (xTaskGetTickCount() - startTick) * portTICK_RATE_MS, heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
}

void setLogLevel(uint8_t level)
{
	if (level == 3)
	{
		level = ESP_LOG_INFO;
	}
	else if (level == 4)
	{
		level = ESP_LOG_DEBUG;
	}
	else if (level == 2)
	{
		level = ESP_LOG_WARN;
	}
	else if (level == 1)
	{
		level = ESP_LOG_ERROR;
	}
	else if (level == 0)
	{
		level = ESP_LOG_NONE;
	}
	else if (level == 5)
	{
		level = ESP_LOG_VERBOSE;
	}

	esp_log_level_set("*", ESP_LOG_ERROR);
	esp_log_level_set("stateConfig", level);
	esp_log_level_set("console", level);
	esp_log_level_set("MAIN", level);
	esp_log_level_set(TAG, level);
	esp_log_level_set("AUDIO", level);
	esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_ERROR);
	esp_log_level_set("MP3_DECODER", ESP_LOG_ERROR);
	esp_log_level_set("CODEC_ELEMENT_HELPER:", ESP_LOG_ERROR);
	esp_log_level_set("FATFS_STREAM", ESP_LOG_ERROR);
	esp_log_level_set("AUDIO_PIPELINE", ESP_LOG_ERROR);
	esp_log_level_set("I2S_STREAM", ESP_LOG_ERROR);
	esp_log_level_set("RSP_FILTER", ESP_LOG_ERROR);
	esp_log_level_set("WIFI", level);
	esp_log_level_set("esp_netif_handlers", level);
	esp_log_level_set("[Ftp]", level);
	esp_log_level_set("system_api", level);
	esp_log_level_set("MDNS", level);
	esp_log_level_set("[Ftp]", level);
	esp_log_level_set("mqtt", level);
	esp_log_level_set("leds", level);
	esp_log_level_set("ST7789", level);
	esp_log_level_set("JPED_Decoder", ESP_LOG_ERROR);
	esp_log_level_set("SDMMC", level);
	esp_log_level_set("ME_SLOT_CONFIG", level);
	esp_log_level_set("BUTTONS", level);
	esp_log_level_set("EXECUTOR", level);
	esp_log_level_set("REPORTER", level);
	esp_log_level_set("LAN", level);
	esp_log_level_set("3n_MOSFET", level);
	esp_log_level_set("RFID", level);
	esp_log_level_set("ENCODERS", level);
	esp_log_level_set("LIDARS", level);
	esp_log_level_set("rotary_encoder", level);
	esp_log_level_set("P9813", level);
}

void mqtt_wait_lan_and_start()
{
	while (me_state.LAN_init_res != ESP_OK)
	{
		vTaskDelay(pdMS_TO_TICKS(100));
	}
	ESP_LOGI(TAG, "network inited, start mqtt");
	mqtt_app_start();
	vTaskDelete(NULL);
}
// void default_app_main(void) {
//
//	relayGPIO_init();
//
//	//spiffs_init();
//	nvs_init();
//
//	ESP_LOGI(TAG, "try init sdCard");
//	//if (sd_card_init() != ESP_OK) {
//
// }

void let_dd_task()
{
	init_p9813(0, 8);
	int color[8][3] = {
		{250, 250, 250},
		{250, 250, 250},
		{250, 250, 250},
		{250, 250, 250},
		{250, 250, 250},
		{250, 0, 0},
		{250, 250, 250},
		{250, 250, 250}};
	int increment = 10;
	uint8_t seg_led_target;
	int16_t led_mass[8][3];
	for (int i = 0; i < 8; i++){
		led_mass[i][0] = 0;
		led_mass[i][1] = 0;
		led_mass[i][2] = 0;
	}

	// for (int i = 0; i < 8; i++){
	// 	p9813_set_led_color(i, 255, 255, 255);
	// }
	// p9813_write_led();
	// vTaskDelay(pdMS_TO_TICKS(2500));
	// for (int i = 0; i < 8; i++){
	// 	p9813_set_led_color(i, 0, 0, 0);
	// }
	// p9813_write_led();


	while (1){
		for (int i = 0; i < 8; i++){
			for (int y = 0; y < 3; y++){
				if (i == led_segment){
					if (led_mass[i][y] < color[i][y]){
						led_mass[i][y] += increment;
						if(led_mass[i][y]>color[i][y]){
							led_mass[i][y]=color[i][y];
						}
					}
				}
				else{
					if (led_mass[i][y] > 0)	{
						led_mass[i][y] -= increment;
						if(led_mass[i][y]<0){
							led_mass[i][y]=0;
						}
					}
				}
			}
			p9813_set_led_color(i, led_mass[i][0], led_mass[i][1], led_mass[i][2]);
			//printf("%d---%d:%d:%d\n", i, led_mass[i][0], led_mass[i][1], led_mass[i][2]);
		}
		// p9813_set_led_color(prev_led_segment,0,0,0);
		// p9813_set_led_color(led_segment,0,255,0);
		p9813_write_led();
		// if (led_segment != prev_led_segment)
		// {
		// 	prev_led_segment = led_segment;
		// }

		vTaskDelay(pdMS_TO_TICKS(25));
	}
}

void app_main(void)
{

	setLogLevel(4);

	ESP_LOGD(TAG, "Start up");
	ESP_LOGD(TAG, "free Heap size %d", xPortGetFreeHeapSize());

	// initLeds();
	board_init(); // USB hardware

	spiffs_init();
	nvs_init();

	xTaskCreateStatic(usb_device_task, "usbd", USBD_STACK_SIZE, NULL, configMAX_PRIORITIES - 1, usb_device_stack, &usb_device_taskdef);
	xTaskCreateStatic(cdc_task, "cdc", 1024 * 4, NULL, configMAX_PRIORITIES - 2, cdc_stack, &cdc_taskdef);
	xTaskCreatePinnedToCore(listenListener, "audio_listener", 1024 * 2, NULL, 1, NULL, 0);
	xTaskCreate(crosslinker_task, "cross_linker", 1024 * 4, NULL, configMAX_PRIORITIES - 8, NULL);
	ESP_LOGD(TAG, "CDC task started");

	exec_mailbox = xQueueCreate(10, sizeof(exec_message_t));
	if (exec_mailbox == NULL)
	{
		ESP_LOGE(TAG, "Exec_Mailbox create FAIL");
	}

	me_state.sd_init_res = spisd_init();
	if (me_state.sd_init_res != ESP_OK)
	{
		ESP_LOGE(TAG, "spisd_init FAIL");
		esp_restart();
	}
	else
	{
		if (remove("/sdcard/error.txt"))
		{
			ESP_LOGD(TAG, "/sdcard/error.txt delete failed");
		}
		load_Default_Config();
		scanFileSystem();
		me_state.config_init_res = loadConfig();
		if (me_state.config_init_res != ESP_OK)
		{
			char tmpString[40];
			sprintf(tmpString, "Load config FAIL in line: %d", me_state.config_init_res);
			writeErrorTxt(tmpString);
		}

		me_state.slot_init_res = init_slots();

		me_state.content_search_res = loadContent();
		if (me_state.content_search_res != ESP_OK)
		{
			ESP_LOGD(TAG, "Load Content FAIL");
			writeErrorTxt("Load content FAIL");
		}

		if (me_config.LAN_enable == 1)
		{
			LAN_init();
		}

		if (strlen(me_config.mqttBrokerAdress) > 3)
		{
			xTaskCreate(mqtt_wait_lan_and_start, "mqtt_wait_lan", 1024 * 4, NULL, configMAX_PRIORITIES - 8, NULL);
		}
	}

	//	me_slot_config(1, BUTTON_OPTORELAY_MODULE);
	//	me_slot_config(2, BUTTON_OPTORELAY_MODULE);
	//	me_slot_config(3, BUTTON_OPTORELAY_MODULE);

	//	for(int i=0; i<me_state.triggers_topic_list_index; i++){
	//		printf("trigger:%s \n", me_state.triggers_topic_list[i]);
	//	}
	//	for(int i=0; i<me_state.action_topic_list_index; i++){
	//		printf("action:%s \n", me_state.action_topic_list[i]);
	//	}
	//
	//	execute("monofon_1/optorelay_1:1");

	//	xTaskCreatePinnedToCore(playerTask, "player", 1024 * 8, NULL, 1, NULL, 0);
	//	vTaskDelay(pdMS_TO_TICKS(100));
	//	xTaskCreatePinnedToCore(sensTask, "sens", 1024 * 4, NULL, 24, NULL, 1);

	// execute("monofon_1/play_track:0");

	// console_init();
	//

	start_encoder_inc_task(2);

	xTaskCreate(let_dd_task, "let_dd_task", 1024 * 4, NULL, configMAX_PRIORITIES - 8, NULL);

	vTaskDelay(pdMS_TO_TICKS(100));
	ESP_LOGI(TAG, "Ver %s. Load complite, start working. free Heap size %d", VERSION, xPortGetFreeHeapSize());

	while (1)
	{

		if (xQueueReceive(exec_mailbox, &exec_message, (25 / portTICK_RATE_MS)) == pdPASS)
		{
			ESP_LOGD(TAG, "Exec mail incoming:%s", exec_message.str);
			// char *event = exec_message.str + strlen(me_config.device_name) + 1;
			execute(exec_message.str);
		}

		if(xTaskGetTickCount()>5000000){
			esp_restart();
		}

		if(flag_ccw){
		flag_ccw=0;
		ESP_LOGD(TAG, "ccw");
	}
	if(flag_cw){
		flag_cw=0;
		ESP_LOGD(TAG, "cw");
	}
	}

	
}

// USB Device Driver task
// This top level thread process all usb events and invoke callbacks
