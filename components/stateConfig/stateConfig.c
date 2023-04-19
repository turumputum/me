#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stateConfig.h"
#include "ini.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "esp_spiffs.h"
#include "ff.h"
#include "diskio.h"
#include "sdcard_scan.h"
#include <errno.h>

#include "audio_error.h"
#include "audio_mem.h"

#include <dirent.h>

#define TAG "stateConfig"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#define SDCARD_SCAN_URL_MAX_LENGTH (255)

extern configuration me_config;
extern stateStruct me_state;

static int handler(void *user, const char *section, const char *name, const char *value) {
	configuration *pconfig = (configuration*) user;

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
	if (MATCH("led", "RGB.r")) {
		pconfig->RGB.r = atoi(value);
	} else if (MATCH("led", "RGB.g")) {
		pconfig->RGB.g = atoi(value);
	} else if (MATCH("led", "RGB.b")) {
		pconfig->RGB.b = atoi(value);
	} else if (MATCH("led", "animate")) {
		pconfig->animate = atoi(value);
	} else if (MATCH("led", "rainbow")) {
		pconfig->rainbow = atoi(value);
	} else if (MATCH("led", "brightMax")) {
		pconfig->brightMax = atoi(value);
	} else if (MATCH("led", "brightMin")) {
		pconfig->brightMin = atoi(value);
	} else if (MATCH("player", "volume")) {
		pconfig->volume = atoi(value);
	} else if (MATCH("player", "playerMode")) {
		pconfig->playerMode = atoi(value);
	} else if (MATCH("player", "monofonEnable")) {
		pconfig->monofonEnable = atoi(value);
	} else if (MATCH("player", "trackEnd_action")) {
		pconfig->trackEnd_action = atoi(value);
	} else if (MATCH("player", "phoneDown_action")) {
		pconfig->phoneDown_action = atoi(value);
	} else if (MATCH("player", "phoneUp_delay")) {
		pconfig->phoneUp_delay = atoi(value);
	} else if (MATCH("player", "relay_inverse")) {
		pconfig->relay_inverse = atoi(value);
	} else if (MATCH("sens", "phoneSensInverted")) {
		pconfig->phoneSensInverted = atoi(value);
	} else if (MATCH("sens", "sensDebug")) {
		pconfig->sensDebug = atoi(value);
	} else if (MATCH("sens", "sensMode")) {
		pconfig->sensMode = atoi(value);
	} else if (MATCH("WIFI", "WIFI_mode")) {
		pconfig->WIFI_mode = atoi(value);
	} else if (MATCH("WIFI", "WIFI_ssid")) {
		pconfig->WIFI_ssid = strdup(value);
	} else if (MATCH("WIFI", "WIFI_pass")) {
		pconfig->WIFI_pass = strdup(value);
	} else if (MATCH("WIFI", "WIFI_channel")) {
		pconfig->WIFI_channel = atoi(value);
	} else if (MATCH("WIFI", "ipAdress")) {
		pconfig->ipAdress = strdup(value);
	} else if (MATCH("WIFI", "netMask")) {
		pconfig->netMask = strdup(value);
	} else if (MATCH("WIFI", "gateWay")) {
		pconfig->gateWay = strdup(value);
	} else if (MATCH("WIFI", "DHCP")) {
		pconfig->DHCP = atoi(value);
	} else if (MATCH("WIFI", "FTP_login")) {
		pconfig->FTP_login = strdup(value);
	} else if (MATCH("WIFI", "FTP_pass")) {
		pconfig->FTP_pass = strdup(value);
	} else if (MATCH("WIFI", "device_name")) {
		pconfig->device_name = strdup(value);
	} else if (MATCH("WIFI", "mqttBrokerAdress")) {
		pconfig->mqttBrokerAdress = strdup(value);
	} else {
		return 0; /* unknown section/name, error */
	}
	return 1;
}

void writeErrorTxt(const char *buff) {

	FILE *errFile;

	errFile = fopen("/sdcard/error.txt", "a");
	if (!errFile) {
		ESP_LOGE(TAG, "fopen() failed");
		return;
	}
	unsigned int bytesWritten;
	bytesWritten = fprintf(errFile, buff);
	if (bytesWritten == 0) {
		ESP_LOGE(TAG, "fwrite() failed");
		return;
	}

	fclose(errFile);

}

void load_Default_Config(void) {
	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();

	me_config.device_name = strdup("monofon_1");

	me_config.WIFI_mode = 0; // disable

	me_config.WIFI_ssid = strdup("");
	me_config.WIFI_pass = strdup("monofonpass");
	me_config.WIFI_channel = 6;

	me_config.DHCP = 0;

	me_config.ipAdress = strdup("10.0.0.1");
	me_config.netMask = strdup("255.255.255.0");
	me_config.gateWay = strdup("10.0.0.1");

	me_config.FTP_login = strdup("user");
	me_config.FTP_pass = strdup("pass");

	me_config.mqttBrokerAdress = strdup("");

	me_config.brightMax = 200;
	me_config.brightMin = 0;
	me_config.RGB.r = 0;
	me_config.RGB.g = 0;
	me_config.RGB.b = 200;
	me_config.animate = 1;
	me_config.rainbow = 0;

	me_config.monofonEnable = 1;
	me_config.trackEnd_action = 0; //stop
	me_config.phoneDown_action = 0; //none
	me_config.playerMode = 1; // SD_card source
	me_config.volume = 60;
	me_config.phoneUp_delay = 500; //0,5 sek
	me_config.relay_inverse = 0;

	me_config.sensMode = 1; //hall sensor
	me_config.phoneSensInverted = 0;
	me_config.sensDebug = 0;

	me_state.numOfTrack = 0;

	ESP_LOGD(TAG, "Load default config complite. Duration: %d ms. Heap usage: %d", (xTaskGetTickCount() - startTick) * portTICK_RATE_MS, heapBefore - xPortGetFreeHeapSize());
}

uint8_t loadConfig(void) {
	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();

	ESP_LOGD(TAG, "Init config");
	int res = ESP_OK;

	if (me_config.configFile[0] != 0) {
		res = ini_parse(me_config.configFile, handler, &me_config);
		if (res != 0) {
			ESP_LOGE(TAG, "Can't load 'config.ini' check line: %d, set default\n", res);
			return res;
		}
	} else {
		ESP_LOGD(TAG, "config file not found, create default config");
		saveConfig();
		return res;
	}

	ESP_LOGD(TAG, "Load config complite. Duration: %d ms. Heap usage: %d", (xTaskGetTickCount() - startTick) * portTICK_RATE_MS, heapBefore - xPortGetFreeHeapSize());
	return res;

//	ESP_LOGD(TAG, "\nLoad config:");
//	ESP_LOGD(TAG, "[LOG]");
//	ESP_LOGD(TAG, "me_config.logLevel = %d", me_config.logLevel);
//	ESP_LOGD(TAG, "[WIFI]");
//	ESP_LOGD(TAG, "me_config.WIFI_mode = %d", me_config.WIFI_mode);
//	ESP_LOGD(TAG, "me_config.WIFI_ssid = %s", me_config.WIFI_ssid);
//	ESP_LOGD(TAG, "me_config.WIFI_pass = %s", me_config.WIFI_pass);
//	ESP_LOGD(TAG, "me_config.WIFI_channel = %d", me_config.WIFI_channel);
//	ESP_LOGD(TAG, "me_config.ipAdress = %s", me_config.ipAdress);
//	ESP_LOGD(TAG, "me_config.netMask = %s", me_config.netMask);
//	ESP_LOGD(TAG, "me_config.gateWay = %s", me_config.gateWay);
//	ESP_LOGD(TAG, "[leds]\n");
//	ESP_LOGD(TAG, "me_config.brightMax = %d", me_config.brightMax);
//	ESP_LOGD(TAG, "me_config.brightMin = %d", me_config.brightMin);
//	ESP_LOGD(TAG, "me_config.RGB.r = %d", me_config.RGB.r);
//	ESP_LOGD(TAG, "me_config.RGB.g = %d", me_config.RGB.g);
//	ESP_LOGD(TAG, "me_config.RGB.b = %d", me_config.RGB.b);
//	ESP_LOGD(TAG, "me_config.animate = %d", me_config.animate);
//	ESP_LOGD(TAG, "me_config.rainbow = %d", me_config.rainbow);
//	ESP_LOGD(TAG, "[sens]");
//	ESP_LOGD(TAG, "me_config.phoneSensInverted = %d", me_config.phoneSensInverted);
//	ESP_LOGD(TAG, "me_config.sensDebug = %d", me_config.sensDebug);

}

int saveConfig(void) {

	ESP_LOGD(TAG, "saving file");

	FILE *configFile;
	char tmp[100];

	if (remove("/sdcard/config.ini")) {
		//ESP_LOGD(TAG, "/sdcard/config.ini delete failed");
		//return ESP_FAIL;
	}

	configFile = fopen("/sdcard/config.ini", "w");
	if (!configFile) {
		ESP_LOGE(TAG, "fopen() failed");
		return ESP_FAIL;
	}

	sprintf(tmp, ";config file Monofon. Ver:%s \r\n\r\n", VERSION);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));

	sprintf(tmp, "\r\n[WIFI] \r\n");
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));

	sprintf(tmp, "device_name = %s \r\n", me_config.device_name);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "WIFI_mode = %d ;0-disable, 1-AP mode, 2-station mode \r\n", me_config.WIFI_mode);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "WIFI_ssid = %s \r\n", me_config.WIFI_ssid);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "WIFI_pass = %s \r\n", me_config.WIFI_pass);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "WIFI_channel = %d \r\n", me_config.WIFI_channel);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "DHCP = %d ;0-disable, 1-enable \r\n", me_config.DHCP);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "ipAdress = %s \r\n", me_config.ipAdress);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "netMask = %s \r\n", me_config.netMask);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "gateWay = %s \r\n", me_config.gateWay);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "FTP_login = %s \r\n", me_config.FTP_login);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "FTP_pass = %s \r\n", me_config.FTP_pass);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "mqttBrokerAdress = %s \r\n", me_config.mqttBrokerAdress);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));

	sprintf(tmp, "\r\n[led] \r\n");
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "brightMax = %d ; 0-255\r\n", me_config.brightMax);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "brightMin = %d ; 0-255\r\n", me_config.brightMin);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "RGB.r = %d ; 0-255 Red color\r\n", me_config.RGB.r);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "RGB.g = %d ; 0-255 Green color\r\n", me_config.RGB.g);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "RGB.b = %d ; 0-255 Blue color\r\n", me_config.RGB.b);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "animate = %d \r\n", me_config.animate);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "rainbow = %d \r\n", me_config.rainbow);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));

	sprintf(tmp, "\r\n[player] \r\n");
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "monofonEnable = %d ; 0 - disable, 1 - Enable \r\n", me_config.monofonEnable);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "playerMode = %d ; 0 - disable, 1 - SD_card source \r\n", me_config.playerMode);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "volume = %d ;\r\n", me_config.volume);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "trackEnd_action = %d ; 0 - stop, 1 - loop, 2 - next \r\n", me_config.trackEnd_action);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "phoneDown_action = %d ; 0 - none, 1 - next \r\n", me_config.phoneDown_action);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "phoneUp_delay = %d ; ms \r\n", me_config.phoneUp_delay);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "relay_inverse = %d ; \r\n", me_config.relay_inverse);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));

	sprintf(tmp, "\r\n[sens] \r\n");
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "sensMode = %d ; 0-disable, 1-hall sensor mode\r\n", me_config.sensMode);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "phoneSensInverted = %d \r\n", me_config.phoneSensInverted);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));
	sprintf(tmp, "sensDebug = %d \r\n", me_config.sensDebug);
	fprintf(configFile, tmp);
	memset(tmp, 0, strlen(tmp));

	vTaskDelay(pdMS_TO_TICKS(100));

	FRESULT res;
	res = fclose(configFile);
	if (res != ESP_OK) {
		ESP_LOGE(TAG, "fclose() failed: %d ", res);
		return ESP_FAIL;
	}

	ESP_LOGD(TAG, "save OK");
	return ESP_OK;
}

/*
 FRESULT scan_in_dir(const char *file_extension, FF_DIR *dp, FILINFO *fno) {
 FRESULT res = FR_OK;

 while (res == FR_OK) {
 res = f_readdir(dp, fno);

 if (dp->sect == 0) {
 ESP_LOGD(TAG, "end of file in dir");
 break;
 }

 if (fno->fattrib & AM_DIR) {
 ESP_LOGD(TAG, "found subDir = %s", fno->fname);
 } else {
 ESP_LOGD(TAG, "found file = %s", fno->fname);
 char *detect = strrchr(fno->fname, '.');

 if (NULL == detect) {
 ESP_LOGD(TAG, "bad file name = %s, res:%d", fno->fname, res);
 // break;
 } else {
 ESP_LOGD(TAG, "found extension:%s", detect);
 detect++;
 if (strcasecmp(detect, file_extension) == 0) {
 ESP_LOGD(TAG, "good file_extension:%s ", fno->fname);
 return FR_OK;
 }
 }
 }
 }
 return -1;
 }

 void search_introIcon(const char *path) {
 FRESULT res;
 FF_DIR dir;
 FILINFO fno;

 res = f_opendir(&dir, path);
 if (res == FR_OK) {
 res = scan_in_dir((const char* ) { "jpg" }, &dir, &fno);
 if (res == FR_OK && fno.fname[0]) {
 sprintf(me_config.introIco, "/sdcard/%s", fno.fname);
 ESP_LOGI(TAG, "found intro image file: %s", me_config.introIco);
 me_state.introIco_error = 0;
 }
 }
 f_closedir(&dir);
 if (me_state.introIco_error == 1) {
 ESP_LOGD(TAG, "introIcon not found(it should be .jpeg/.jpg 240x240px)");
 }
 }

 uint8_t search_contenInDir(const char *path) {
 FRESULT res;
 FF_DIR dir;
 FILINFO fno;

 res = f_opendir(&dir, path);
 if (res == FR_OK) {
 ESP_LOGD(TAG, "Open dir: %s", fno.fname);
 res = scan_in_dir((const char* ) { "mp3" }, &dir, &fno);
 if (res == FR_OK) {

 memset(me_config.tracks[me_state.numOfTrack].audioFile, 0, 254);
 if (strcmp(path, "/") == 0) {
 sprintf(me_config.tracks[me_state.numOfTrack].audioFile, "/sdcard/%s", fno.fname);
 } else {
 sprintf(me_config.tracks[me_state.numOfTrack].audioFile, "/sdcard/%s/%s", path, fno.fname);
 }

 ESP_LOGI(TAG, "found audio file: %s num: %d", me_config.tracks[me_state.numOfTrack].audioFile, me_state.numOfTrack);
 }
 }
 f_closedir(&dir);

 res = f_opendir(&dir, path);
 if (res == FR_OK) {
 res = scan_in_dir((const char* ) { "jpg" }, &dir, &fno);
 if (res == FR_OK) {

 memset(me_config.tracks[me_state.numOfTrack].icoFile, 0, 254);
 if (strcmp(path, "/") == 0) {
 sprintf(me_config.tracks[me_state.numOfTrack].icoFile, "/sdcard/%s", fno.fname);
 } else {
 sprintf(me_config.tracks[me_state.numOfTrack].icoFile, "/sdcard/%s/%s", path, fno.fname);
 }

 ESP_LOGI(TAG, "found image file: %s num: %d", me_config.tracks[me_state.numOfTrack].icoFile, me_state.numOfTrack);

 }
 }
 f_closedir(&dir);
 if (me_config.tracks[me_state.numOfTrack].audioFile[0] != 0) {
 me_state.numOfTrack++;
 }
 return me_state.numOfTrack;
 }
 */

uint8_t loadContent(void) {
	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();
	ESP_LOGD(TAG, "Loading content");

	if (me_state.numOfTrack > 0) {
		ESP_LOGI(TAG, "Load Content complete. numOfTrack:%d Duration: %d ms. Heap usage: %d", me_state.numOfTrack, (xTaskGetTickCount() - startTick) * portTICK_RATE_MS,
				heapBefore - xPortGetFreeHeapSize());
		return ESP_OK;
	} else {
		ESP_LOGE(TAG, "Load content fail");
		return ESP_FAIL;
	}
}

uint8_t scan_dir(const char *path) {
	FRESULT res;
	FF_DIR dir;
	FILINFO fno;
	uint8_t picIndex = 0;
	uint8_t soundIndex = 0;

	res = f_opendir(&dir, path);
	if (res == FR_OK) {
		ESP_LOGD(TAG, "{scanFileSystem} Open dir: %s", path);
		while (1) {
			res = f_readdir(&dir, &fno); /* Read a directory item */
			ESP_LOGD(TAG, "{scanFileSystem} File object:%s size:%d dir:%d hid:%d", fno.fname, fno.fsize, (fno.fattrib & AM_DIR), (fno.fattrib & AM_HID));
			if ((res == FR_OK) && (fno.fname[0] != 0)) {
				if (!(fno.fattrib & AM_HID)) {
					if (fno.fattrib & AM_DIR) {
						scan_dir(fno.fname);
					} else {
						// file founded
						if (strcasecmp(fno.fname, "config.ini") == 0) {
							//ESP_LOGD(TAG, "{scanFileSystem} config file founded ");
							sprintf(me_config.configFile, "/sdcard/%s", fno.fname);
						} else if (strcasecmp(fno.fname, "intro.jpg") == 0) {
							//ESP_LOGD(TAG, "{scanFileSystem} introIco file founded ");
							sprintf(me_config.introIco, "/sdcard/%s", fno.fname);
						} else {
							char *detect = strrchr(fno.fname, '.');
							//ESP_LOGD(TAG, "{scanFileSystem} cut extension: %s ", detect);
							if (strcasecmp(detect, ".mp3") == 0) {
								//ESP_LOGD(TAG, "{scanFileSystem} soundFile founded ");
								if (strcmp(path, "/") == 0) {
									sprintf(me_config.soundTracks[soundIndex], "/sdcard/%s", fno.fname);
								} else {
									sprintf(me_config.soundTracks[soundIndex], "/sdcard/%s/%s", path, fno.fname);
								}
								soundIndex++;
							} else if (strcasecmp(detect, ".jpg") == 0) {
								//ESP_LOGD(TAG, "{scanFileSystem} iconFile founded ");
								if (strcmp(path, "/") == 0) {
									sprintf(me_config.trackIcons[picIndex], "/sdcard/%s", fno.fname);
								} else {
									sprintf(me_config.trackIcons[picIndex], "/sdcard/%s/%s", path, fno.fname);
								}
								picIndex++;
							}
						}
						//ESP_LOGD(TAG, "{scanFileSystem} File found ");
					}
				}
			} else {
				break;
			}
		}
	}
	return soundIndex;
}

void printTrackList() {
	for (int i = 0; i < MAX_NUM_OF_TRACKS; i++) {
		ESP_LOGD(TAG, "{scanFileSystem} num:%d --- %s --- %s", i, (char* )me_config.soundTracks[i], (char* )me_config.trackIcons[i]);
	}
}

uint8_t scanFileSystem() {
	uint32_t startTick = xTaskGetTickCount();
	uint32_t heapBefore = xPortGetFreeHeapSize();
	ESP_LOGD(TAG, "scanFileSystem");

	me_state.numOfTrack = 0;
	me_state.introIco_error = 1;
	memset(me_config.configFile, 0, 254);
	memset(me_config.introIco, 0, 254);
	for (int i = 0; i < MAX_NUM_OF_TRACKS; i++) {
		memset(me_config.soundTracks[i], 0, 254);
		memset(me_config.trackIcons[i], 0, 254);
	}

//	FRESULT res;
//	FF_DIR dir;
//	FILINFO fno;

	//scan_dir res = f_opendir(&dir, "/");
	me_state.numOfTrack = scan_dir("/");
	ESP_LOGD(TAG, "scan end, lets sort");
	//printTrackList();

	//---sorting sound trucks---
	if (me_state.numOfTrack > 1) {
		qsort(me_config.soundTracks, me_state.numOfTrack, FILE_NAME_LEGHT, (int (*)(const void*, const void*)) strcmp);
	}
	ESP_LOGD(TAG, "sort end, lets associate pic");
	//printTrackList();
	//---associate pic to track---
	for (int n = 0; n < me_state.numOfTrack; n++) {
		char *track_tmp;
		track_tmp = strdup(me_config.soundTracks[n]);
		if (strcmp(track_tmp, "") == 0) {
			char *name_sound = strtok(track_tmp, ".");
			if (strcmp(name_sound, "") == 0) {
				ESP_LOGD(TAG, "{scanFileSystem} search icon for: %s", name_sound);
				for (int i = 0; i < MAX_NUM_OF_TRACKS; i++) {

					if (strcmp(me_config.trackIcons[i], "") == 0) {
						char *icon_tmp;
						icon_tmp = strdup(me_config.trackIcons[i]);
						ESP_LOGD(TAG, "{scanFileSystem} Icon file: %s", me_config.trackIcons[i]);

						char *name_pic = strtok(icon_tmp, ".");
						if (strcmp(name_pic, "") == 0) {
							ESP_LOGD(TAG, "{scanFileSystem} validate: %s", name_pic);
							if (strcmp(name_sound, name_pic) == 0) {
								char s_tmp[FILE_NAME_LEGHT];
								sprintf(s_tmp, "%s", me_config.trackIcons[n]);
								memset(me_config.trackIcons[n], 0, 254);
								sprintf(me_config.trackIcons[n], "%s", me_config.trackIcons[i]);
								memset(me_config.trackIcons[i], 0, 254);
								sprintf(me_config.trackIcons[i], "%s", s_tmp);
								//ESP_LOGD(TAG,"{scanFileSystem} name is correct");
							}
						}
					}
				}
				//--- remove unused images from list ---
				ESP_LOGD(TAG, "{scanFileSystem} remove unused pic ");
				if (strcmp(me_config.trackIcons[n], "") == 0) {
					char *icon_tmp;
					icon_tmp = strdup(me_config.trackIcons[n]);
					char *name_pic = strtok(icon_tmp, ".");
					if (strcmp(name_sound, name_pic) != 0) {
						memset(me_config.trackIcons[n], 0, 254);
					}
				}
				ESP_LOGD(TAG, "{scanFileSystem} unused pic deleted from list");
			}
		}
	}
	printTrackList();

	ESP_LOGI(TAG, "Filesystem scan complete. numOfTrack:%d Duration: %d ms. Heap usage: %d", me_state.numOfTrack, (xTaskGetTickCount() - startTick) * portTICK_RATE_MS,
			heapBefore - xPortGetFreeHeapSize());
	return ESP_OK;

}

