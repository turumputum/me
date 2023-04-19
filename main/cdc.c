#include "tusb.h"
#include "stdbool.h"
#include "stateConfig.h"
#include "esp_wifi.h"

#include "WIFI.h"

#include "audioPlayer.h"


#define USB_PRINT_DELAY 10

extern void setMscEnabledOriginal(int ena);
extern int isMscEnabled();

int flagListDir = 0;

uint8_t tmpbuf[128];

static int strBuffPtr = 0;
static char strBuff[128];

static char printfBuff[256];
extern configuration me_config;
extern stateStruct me_state;

extern uint8_t FLAG_PC_AVAILEBLE;
extern uint8_t FLAG_PC_EJECT;

void usbprintf(char *msg, ...) {
	va_list list;
	va_list args;
	int len;

	va_start(list, msg);
	va_copy(args, list);
	va_end(list);

	if ((len = vsprintf(printfBuff, msg, args)) > 0) {
		tud_cdc_write(printfBuff, len);
		tud_cdc_write_flush();
	}
}

void usbprint(char *msg) {
	int cutLength = 63;
	char partBuff[cutLength + 1];
	int endPartLength = strlen(msg);
	char *endPart = msg;

	while (endPartLength > cutLength) {
		sprintf(partBuff, "%.*s", cutLength, endPart);
		endPart = endPart + cutLength;
		//printf("%s --- \r\n", partBuff);
		tud_cdc_write(partBuff, strlen(partBuff));
		tud_cdc_write_flush();
		vTaskDelay(pdMS_TO_TICKS(USB_PRINT_DELAY));
		endPartLength = strlen(endPart);
	}

	tud_cdc_write(endPart, strlen(endPart));
	tud_cdc_write_flush();
	vTaskDelay(pdMS_TO_TICKS(USB_PRINT_DELAY));
	//printf("%s  \r\n", endPart);

}

static void execCommand(char *cmd, int len) {
	if (!memcmp(cmd, "set msc_enable", 14)) {
		if (atoi(cmd + 14)) {
			if (isMscEnabled()) {
				usbprint("\r\nMSC already ON\r\n");
			} else {
				usbprint("\r\nturning MSC on\r\n");
				//esp_restart();
				setMscEnabledOriginal(1);
			}
		} else {
			if (!isMscEnabled()) {
				usbprint("\r\nMSC already OFF\r\n");
			} else {
				usbprint("\r\nturning MSC off\r\n");
				flagListDir = 1;
				setMscEnabledOriginal(0);
			}
		}
	} else if (!memcmp(cmd, "help", 4)) {
		if (FLAG_PC_EJECT == 0) {
			usbprint("Monofon in mass storage device mode\n"
					"List of avalible commands:\n\n"
					"<Who are you?> - get device identifier and name\n\n"
					"<set msc_enable 'val'> - enable/disable MSD mode, example: <set msc_enable 0>\n");
		} else {
			usbprint("Monofon in player mode\n"
					"List of avalible commands\n\n"
					"<Who are you?> - get device identifier and name\n\n"
					"<get wifi_status> - get wireless network status\n\n"
					"<get system_status> - get system status\n\n"
					"<set msc_enable 'val'> - enable/disable MSD mode, example: <set msc_enable 0>\n\n"
					"<set monofon_enable 'val'> - enable/disable device, example: <set enable 1>\n\n"
					"<set RGB 'val_R' 'val_G' 'val_B'> - set light color, example: <set RGB 255 0 0>\n\n"
					"<set volume 'val'> set volume in %, example: <set volume 80>\n\n"
					"<reset config> set default configuration\n\n");
		}
	} else if (!memcmp(cmd, "Who are you?", 12)) {
		char tmpStr[64];
		if (FLAG_PC_EJECT == 0) {
			sprintf(tmpStr, "monofonMSD\n");
		} else if (FLAG_PC_EJECT == 1) {
			sprintf(tmpStr, "monofon:%s\n\n", me_config.device_name);
		}
		usbprint(tmpStr);
	} else if (!memcmp(cmd, "get wifi_status", 15)) {

		if (FLAG_PC_EJECT == 1) {
			char tmpStr[128];
			if (me_state.wifi_error == 1) {
				usbprint("Wifi connection error, 'error.txt' generated\r\n");
			} else {
				if (me_config.WIFI_mode == 0) {
					usbprint("Wifi disabled \r\n");
				} else if (me_config.WIFI_mode == 1) {
					sprintf(tmpStr, "Wifi softap mode.  SSID:%s password:%s channel:%d \r\n", me_config.ssidT, me_config.WIFI_pass, me_config.WIFI_channel);
					usbprint(tmpStr);
					//printf("Wifi clients: %s \r\n", me_state.wifiApClientString);
				} else if (me_config.WIFI_mode == 2) {
					wifi_ap_record_t ap;
					esp_wifi_sta_get_ap_info(&ap);
					sprintf(tmpStr, "Wifi client mode.  SSID:%s password:%s rssi:%d\r\n", me_config.WIFI_ssid, me_config.WIFI_pass, ap.rssi);
					usbprint(tmpStr);
				}
			}
		}
	} else if (!memcmp(cmd, "get system_status", 17)) {
		if (FLAG_PC_EJECT == 1) {
			char tmpStr[128];
			sprintf(tmpStr, "free Heap size %d \r\n", xPortGetFreeHeapSize());
			usbprint(tmpStr);
			if (me_state.sd_error == 1) {
				usbprint("SD_card error\r\n");
			}
			if (me_state.config_error == 1) {
				usbprint("Config error\r\n");
			}
			if (me_state.content_error == 1) {
				usbprint("Content error\r\n");
			}
			if (me_state.wifi_error == 1) {
				usbprint("wifi error\r\n");
			}
			if (me_state.mqtt_error == 1) {
				usbprint("mqtt error\r\n");
			}

			usbprint("Task Name\tStatus\tPrio\tHWM\tTask\tAffinity\n");
			char stats_buffer[1024];
			vTaskList(stats_buffer);
			//sprintf(tmpStr,"%s\n", stats_buffer);
			usbprint(stats_buffer);
			usbprint("\r\n");
		}
	} else if (!memcmp(cmd, "set monofon_enable", 17)) {
		if (FLAG_PC_EJECT == 1) {
			me_config.monofonEnable = atoi(cmd + 18);
			saveConfig();
			usbprint("OK\r\n");
		}
	} else if (!memcmp(cmd, "set RGB", 7)) {
		if (FLAG_PC_EJECT == 1) {
			char *token;
			uint8_t R, G, B;
			token = strtok(cmd, " ");
			printf("tok: %s \r\n", token);
			token = strtok(NULL, " ");
			printf("tok: %s \r\n", token);
			token = strtok(NULL, " ");
			printf("tok: %s \r\n", token);
			R = atoi(token);
			token = strtok(NULL, " ");
			printf("tok: %s \r\n", token);
			G = atoi(token);
			token = strtok(NULL, " ");
			printf("tok: %s \r\n", token);
			B = atoi(token);
			me_config.RGB.r=R;
			me_config.RGB.b=B;
			me_config.RGB.g=G;
			saveConfig();
			usbprint("OK\r\n");
		}
	} else if (!memcmp(cmd, "set volume", 10)) {
		if (FLAG_PC_EJECT == 1) {
			me_config.volume = atoi(cmd + 11);
			setVolume(me_config.volume);
			saveConfig();
			usbprint("OK\r\n");
		}
	} else if (!memcmp(cmd, "reset config", 12)) {
		if (FLAG_PC_EJECT == 1) {
			remove("/sdcard/config.ini");
			usbprint("OK\r\n Device will be rebooted\r\n");
			vTaskDelay(pdMS_TO_TICKS(USB_PRINT_DELAY));
			esp_restart();
		}
	} else if (len > 3) {
		usbprintf("unknown commnad: %s \n", cmd);
	}
}

//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+
void cdc_task(void *params) {
	(void) params;

	// RTOS forever loop
	while (1) {
		// connected() check for DTR bit
		// Most but not all terminal client set this when making connection
		if (tud_cdc_connected()) {
//      if (flagListDir)
//      {
//        spisd_list_root();
//
//        flagListDir = 0;
//      }

			// connected and there are data available
			if (tud_cdc_available()) {
				char *on = (char*) &tmpbuf[0];

				uint32_t count = tud_cdc_read(tmpbuf, sizeof(tmpbuf));

				tud_cdc_write(tmpbuf, count);
				tud_cdc_write_flush();

				while (count--) {
					// dumb overrun protection
					if (strBuffPtr >= sizeof(strBuff)) {
						strBuffPtr = 0;
					}

					if (('\r' == *on) || ('\n' == *on)) {
						strBuff[strBuffPtr] = 0;

						execCommand(strBuff, strBuffPtr);

						strBuffPtr = 0;
					} else {
						strBuff[strBuffPtr] = *on;

						strBuffPtr++;
					}

					on++;
				}
			}
		}

		vTaskDelay(1);
	}
}

// Invoked when cdc when line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
	(void) itf;
	(void) rts;

	// TODO set some indicator
	if (dtr) {
		// Terminal connected
	} else {
		// Terminal disconnected
	}
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf) {
	(void) itf;
}
