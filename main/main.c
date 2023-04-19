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
//#include "sdmmc_cmd.h"
//#include "driver/sdmmc_host.h"
//#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "sdcard_list.h"
#include "sdcard_scan.h"

#include "driver/adc.h"
#include "esp_adc_cal.h"

#include "leds.h"

#include "stateConfig.h"

#include "audioPlayer.h"

#include "WIFI.h"
#include "mdns.h"

#include "ftp.h"


#include "lwip/dns.h"

#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "esp_vfs_cdcacm.h"
#include "myMqtt.h"
#include "cmd.h"
#include "tusb.h"
#include "cdc.h"
extern void board_init(void);

#define USBD_STACK_SIZE     4096

#define MDNS_ENABLE_DEBUG 0

static const char *TAG = "MAIN";

#define SPI_DMA_CHAN 1
//#define MOUNT_POINT "/sdcard"
//static const char *MOUNT_POINT = "/sdcard";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

int FTP_TASK_FINISH_BIT = BIT2;
EventGroupHandle_t xEventTask;

extern uint8_t FTP_SESSION_START_FLAG;
extern uint8_t FLAG_PC_AVAILEBLE;
extern uint8_t FLAG_PC_EJECT;

RTC_NOINIT_ATTR int RTC_flagMscEnabled;
int flagMscEnabled;

// static task
StackType_t usb_device_stack[USBD_STACK_SIZE];
StaticTask_t usb_device_taskdef;
// static task for cdc
//#define CDC_STACK_SZIE      (configMINIMAL_STACK_SIZE*3)
//StackType_t cdc_stack[CDC_STACK_SZIE];
//StaticTask_t cdc_taskdef;
//void usb_device_task(void *param);
//void cdc_task(void *params);

int isMscEnabled() {
	return flagMscEnabled;
}

#define RTC_FLAG_DISABLED_VALUE		0xAAAA5555

configuration me_config;
stateStruct me_state;

uint32_t ADC_AVERAGE;

#define RELAY_1_GPIO GPIO_NUM_18
#define RELAY_2_GPIO GPIO_NUM_48

void listenListener(void *pvParameters);

void RTC_IRAM_ATTR storeMscFlag() {
	RTC_flagMscEnabled = flagMscEnabled ? 0 : RTC_FLAG_DISABLED_VALUE;
}

void RTC_IRAM_ATTR loadMscFlag() {
	flagMscEnabled = RTC_flagMscEnabled != RTC_FLAG_DISABLED_VALUE;

	//printf("@@@@@@@@@@@@@@@@@@@@@@@@ flagMscEnabled = %d\n", flagMscEnabled);
}

void setMscEnabledOriginal(int ena) {
	flagMscEnabled = ena;
	storeMscFlag();
	esp_restart();
}

static void sensTask(void *arg) {

#define MAX_TICK 20
#define BT_THRESHOLD 10

#define IN_1_PIN 17
#define IN_2_PIN 2
#define IN_3_PIN 3
	////////////ADC config////////////////
	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();

	int pinmap_mass[8];
	pinmap_mass[0] = IN_1_PIN;
	pinmap_mass[1] = IN_2_PIN;
	pinmap_mass[2] = IN_3_PIN;

	gpio_reset_pin(IN_1_PIN);
	gpio_reset_pin(IN_2_PIN);
	gpio_reset_pin(IN_3_PIN);

	gpio_set_direction(IN_1_PIN, GPIO_MODE_INPUT);
	gpio_set_direction(IN_2_PIN, GPIO_MODE_INPUT);
	gpio_set_direction(IN_3_PIN, GPIO_MODE_INPUT);

	uint8_t prevSens_state, sens_state;
	int bt_tick_mass[8];

//	ESP_LOGD("SENS",
//			"SNS init complite, start val:%d. Duration: %d ms. Heap usage: %d free heap:%d",
//			average_min, (xTaskGetTickCount() - startTick) * portTICK_RATE_MS,
//			heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());

	while (1) {
		for (int i = 0; i < 3; i++) {
			if (gpio_get_level(pinmap_mass[i]) == 1) {
				bt_tick_mass[i]++;
				if (bt_tick_mass[i] > MAX_TICK) {
					bt_tick_mass[i] = MAX_TICK;
				}
			} else {
				bt_tick_mass[i]--;
				if (bt_tick_mass[i] < 0) {
					bt_tick_mass[i] = 0;
				}
			}

			if(bt_tick_mass[i]>BT_THRESHOLD){
				me_state.bt_state_mass[i]=1;
			}else{
				me_state.bt_state_mass[i]=0;
			}

		}
		vTaskDelay(pdMS_TO_TICKS(25));
	}
}

void set_relay(int8_t num) {
	if (num < 0) {
		gpio_set_level(RELAY_1_GPIO, !me_config.relay_inverse);
		gpio_set_level(RELAY_2_GPIO, !me_config.relay_inverse);
	} else if (num == 0) {
		gpio_set_level(RELAY_1_GPIO, me_config.relay_inverse);
		gpio_set_level(RELAY_2_GPIO, !me_config.relay_inverse);
	} else if (num == 1) {
		gpio_set_level(RELAY_1_GPIO, !me_config.relay_inverse);
		gpio_set_level(RELAY_2_GPIO, me_config.relay_inverse);
	} else if (num > 1) {
		gpio_set_level(RELAY_1_GPIO, me_config.relay_inverse);
		gpio_set_level(RELAY_2_GPIO, me_config.relay_inverse);
	}
}

static void playerTask(void *arg) {
	ESP_LOGD(TAG, "Create fatfs stream to read data from sdcard");
	audioInit();

	char payload[15];
	xTaskCreatePinnedToCore(listenListener, "audio_listener", 1024 * 3, NULL, 1,
	NULL, 0);

	while (1) {
		//listenListener();

		vTaskDelay(pdMS_TO_TICKS(100));

		if ((me_config.playerMode == 1)
				&& (me_config.monofonEnable == 1)) {
//			if (me_state.phoneUp != me_state.prevPhoneUp) {
//
//				printf("phoneUp %d \r\n", me_state.phoneUp);
//				usbprintf("phoneUp %d \r\n", me_state.phoneUp);

				if (me_state.bt_state_mass[0] == 1) {

					vTaskDelay(pdMS_TO_TICKS(me_config.phoneUp_delay));


					audioPlay();

					set_relay(me_state.currentTrack);

				} else {
					//--- phoneDown ---
					set_relay(-1);

					audioStop();

					//--- next track phoneDown ---
					if (me_config.phoneDown_action == 1) {
						me_state.currentTrack++;
						if (me_state.currentTrack
								>= me_state.numOfTrack) {
							me_state.currentTrack = 0;
						}
					}

				}

				me_state.prevPhoneUp = me_state.phoneUp;
			//}

			if (me_state.changeTrack == 1) {
				me_state.changeTrack = 0;
				me_state.currentTrack++;
				if (me_state.currentTrack >= me_state.numOfTrack) {
					me_state.currentTrack = 0;
				}
				audioStop();
				usbprintf("changeTrack %d \r\n", me_state.currentTrack);

				//if (me_config.trackIcons[me_state.currentTrack][0] != 0) { // @suppress("Field cannot be resolved")
				//}
				set_relay(me_state.currentTrack);

				audioPlay();

			}
		}
	}
	free(payload);
}

void ftp_task(void *pvParameters);

void nvs_init() {
	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();
	esp_err_t ret;
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	ESP_LOGD(TAG,
			"NVS init complite. Duration: %d ms. Heap usage: %d free heap:%d",
			(xTaskGetTickCount() - startTick) * portTICK_RATE_MS,
			heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
}

void spiffs_init() {

	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();
	esp_err_t ret;
	esp_vfs_spiffs_conf_t conf = { .base_path = "/spiffs", .partition_label =
	NULL, .max_files = 10, .format_if_mount_failed = true };
	ret = esp_vfs_spiffs_register(&conf);
	size_t total = 0, used = 0;
	ret = esp_spiffs_info(NULL, &total, &used);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)",
				esp_err_to_name(ret));
	} else {
		ESP_LOGD(TAG,
				"SPIFFS init complite. Duration: %d ms. Heap usage: %d free heap:%d",
				(xTaskGetTickCount() - startTick) * portTICK_RATE_MS,
				heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
		ESP_LOGD(TAG, "Partition size: total: %d, used: %d", total, used);
	}
}

void relayGPIO_init() {
	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();

#define GPIO_OUTPUT_IO_0    47
#define GPIO_OUTPUT_IO_1    48
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_1))

	//zero-initialize the config structure.
	gpio_config_t io_conf = { };
	//disable interrupt
	io_conf.intr_type = GPIO_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);

	ESP_LOGD(TAG,
			"GPIO init complite. Duration: %d ms. Heap usage: %d free heap:%d",
			(xTaskGetTickCount() - startTick) * portTICK_RATE_MS,
			heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
}

//int my_mega_fn(void) {
//
//	return 1;
//}

void mdns_start(void) {
	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();
	char mdnsName[80];

	// set mDNS hostname (required if you want to advertise services)
	if (strlen(me_config.device_name) == 0) {
		sprintf(mdnsName, "%s", (char*) me_config.ssidT);
		strcpy(mdnsName, me_config.ssidT);
		ESP_LOGD(TAG, "Set mdns name: %s  device_name len:%d ", mdnsName,
				strlen(me_config.device_name));
	} else {
		sprintf(mdnsName, "%s", me_config.device_name);
		ESP_LOGD(TAG, "Set mdns name: %s  device_name len:%d ", mdnsName,
				strlen(me_config.device_name));
	}

	ESP_ERROR_CHECK(mdns_init());
	ESP_ERROR_CHECK(mdns_hostname_set(mdnsName));
	ESP_ERROR_CHECK(mdns_instance_name_set("monofon-instance"));
	mdns_service_add(NULL, "_ftp", "_tcp", 21, NULL, 0);
	mdns_service_instance_name_set("_ftp", "_tcp", "Monofon FTP server");
	strcat(mdnsName, ".local");
	ESP_LOGI(TAG, "mdns hostname set to: %s", mdnsName);
	mdns_txt_item_t serviceTxtData[1] = { { "URL", strdup(mdnsName) }, };
	//sprintf()
	mdns_service_txt_set("_ftp", "_tcp", serviceTxtData, 1);

	ESP_LOGD(TAG,
			"mdns_start complite. Duration: %d ms. Heap usage: %d free heap:%d",
			(xTaskGetTickCount() - startTick) * portTICK_RATE_MS,
			heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
}

void console_init() {
	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();

	esp_console_repl_t *repl = NULL;
	esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
	esp_console_dev_uart_config_t uart_config =
			ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
	/* Prompt to be printed before each line.
	 * This can be customized, made dynamic, etc.
	 */
	repl_config.prompt = "";
	//repl_config.max_cmdline_length = 150;

	esp_console_register_help_command();
	ESP_ERROR_CHECK(
			esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

	//esp_console_dev_usb_cdc_config_t cdc_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
	//ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&cdc_config,&repl_config, &repl));

	register_console_cmd();

	ESP_LOGD(TAG,
			"Console init complite. Duration: %d ms. Heap usage: %d free heap:%d",
			(xTaskGetTickCount() - startTick) * portTICK_RATE_MS,
			heapBefore - xPortGetFreeHeapSize(), xPortGetFreeHeapSize());
	ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

void setLogLevel(uint8_t level) {
	if (level == 3) {
		level = ESP_LOG_INFO;
	} else if (level == 4) {
		level = ESP_LOG_DEBUG;
	} else if (level == 2) {
		level = ESP_LOG_WARN;
	} else if (level == 1) {
		level = ESP_LOG_ERROR;
	} else if (level == 0) {
		level = ESP_LOG_NONE;
	} else if (level == 5) {
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
	esp_log_level_set("SENS", level);
	esp_log_level_set("SDMMC", level);

}

void default_app_main(void) {

	relayGPIO_init();

	//spiffs_init();
	nvs_init();

	ESP_LOGI(TAG, "try init sdCard");
	//if (sd_card_init() != ESP_OK) {
	if (spisd_init() != ESP_OK) {

		ESP_LOGE(TAG, "spisd_init FAIL");

		me_state.sd_error = 1;
		esp_restart();
	} else {
		//spisd_mount_fs();
		me_state.sd_error = 0;
		if (remove("/sdcard/error.txt")) {
			ESP_LOGD(TAG, "/sdcard/error.txt delete failed");
		}
		//setMscEnabled(1);
		//setMscEnabled(0);

		load_Default_Config();
		scanFileSystem();
		int res = loadConfig();
		if (res == ESP_OK) {
			me_state.config_error = 0;
		} else {
			char tmpString[40];
			sprintf(tmpString, "Load config FAIL in line: %d", res);
			writeErrorTxt(tmpString);
			me_state.config_error = 1;
		}

		if (loadContent() == ESP_OK) {
			me_state.content_error = 0;
		} else {
			ESP_LOGD(TAG, "Load Content FAIL");
			writeErrorTxt("Load content FAIL");
			me_state.content_error = 1;
		}

		if (wifiInit() != ESP_OK) {
			me_state.wifi_error = 1;
			wifi_scan();
		} else {

			if (me_config.WIFI_mode != 0) {

				xTaskCreatePinnedToCore(ftp_task, "FTP", 1024 * 6, NULL, 2,
						NULL, 0);

				vTaskDelay(pdMS_TO_TICKS(100));
				mdns_start();
				//mqtt_app_start();
			}
		}

	}

	xTaskCreatePinnedToCore(playerTask, "player", 1024 * 8, NULL, 1, NULL, 0);
	vTaskDelay(pdMS_TO_TICKS(100));
	xTaskCreatePinnedToCore(sensTask, "sens", 1024 * 4, NULL, 24, NULL, 1);

	//console_init();

	vTaskDelay(pdMS_TO_TICKS(100));

	//scanFileSystem();

	ESP_LOGI(TAG, "Ver %s. Load complite, start working. free Heap size %d",VERSION,xPortGetFreeHeapSize());

	while (1) {

		vTaskDelay(pdMS_TO_TICKS(100));
		if (me_state.mqtt_error == 0) {
			char tmpString[10];
			sprintf(tmpString, "%d", pdTICKS_TO_MS(xTaskGetTickCount()) / 1000);
			//mqtt_pub(lifeTime_topic,tmpString);
		}
//		if (me_state.changeTrack == 1) {
//			ESP_LOGI(TAG, "State to:%d", me_state.bt_state_mass[0]);
//			//me_state.changeLang = 0;
//		}

	}
}

void app_main(void) {

	setLogLevel(4);
	initLeds();
	board_init();
//	relayGPIO_init();

	spiffs_init();
//	nvs_init();

	ESP_LOGD(TAG, "Start up");
	ESP_LOGD(TAG, "free Heap size %d", xPortGetFreeHeapSize());

	xTaskCreateStatic(usb_device_task, "usbd", USBD_STACK_SIZE, NULL,
			configMAX_PRIORITIES - 1, usb_device_stack, &usb_device_taskdef);
	xTaskCreateStatic(cdc_task, "cdc", CDC_STACK_SZIE, NULL,
			configMAX_PRIORITIES - 2, cdc_stack, &cdc_taskdef);
	ESP_LOGD(TAG, "CDC task started");
	loadMscFlag();

	if (isMscEnabled()) {
		spisd_init();

		ESP_LOGD(TAG, "@@@@@@@ checking USB connected...");

		for (int i = 0; !FLAG_PC_AVAILEBLE && (i < 40); i++) {
			vTaskDelay(pdMS_TO_TICKS(100));
		}

		ESP_LOGD(TAG, "@@@@@@@ FLAG_PC_AVAILEBLE = %d", FLAG_PC_AVAILEBLE);

		if (FLAG_PC_AVAILEBLE) {
			ESP_LOGD(TAG, "@@@@@@@ WORKING AS MSC + CDC");

			while (1) {
				vTaskDelay(pdMS_TO_TICKS(100));
				if (FLAG_PC_EJECT == 1) {
					vTaskDelay(pdMS_TO_TICKS(1000));
					setMscEnabledOriginal(false);
				}
			}
		} else {
			ESP_LOGD(TAG, "@@@@@@@ DISABLING MSC AND REBOOTING");
			setMscEnabledOriginal(false);
		}
	} else {
		ESP_LOGD(TAG, "@@@@@@@ WORKING AS DEFAULT");

		default_app_main();
	}
}

// USB Device Driver task
// This top level thread process all usb events and invoke callbacks
extern void reconnectUsb();

void usb_device_task(void *param) {
	(void) param;

	// init device stack on configured roothub port
	// This should be called after scheduler/kernel is started.
	// Otherwise it could cause kernel issue since USB IRQ handler does use RTOS queue API.
	tud_init(BOARD_TUD_RHPORT);

	reconnectUsb();

	// RTOS forever loop
	while (1) {
		// put this thread to waiting state until there is new events
		tud_task();

		// following code only run if tud_task() process at least 1 event
		tud_cdc_write_flush();
	}
}
