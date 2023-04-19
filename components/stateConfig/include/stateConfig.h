#include "leds.h"
#include <ff.h>

#define MAX_NUM_OF_TRACKS 10
#define FILE_NAME_LEGHT 300

static const char *VERSION = "1.0";

typedef enum {
    LED_STATE_DISABLE = 0,
    LED_STATE_SD_ERROR,
    LED_STATE_CONFIG_ERROR,
    LED_STATE_CONTENT_ERROR,
	LED_STATE_SENSOR_ERROR,
    LED_STATE_WIFI_FAIL,
	LED_STATE_WIFI_OK,
    LED_STATE_STANDBY,
    LED_STATE_PLAY,
    LED_STATE_FTP_SESSION,
	LED_STATE_MSD_WORK,
} led_state_t;

typedef struct {
	uint8_t changeTrack;
	uint8_t currentTrack;
	uint8_t numOfTrack;
	uint8_t phoneUp;
	uint8_t prevPhoneUp;

	char * wifiApClientString;

	uint8_t sd_error;
	uint8_t config_error;
	uint8_t content_error;
	uint8_t wifi_error;
	uint8_t mqtt_error;
	uint8_t introIco_error;

	led_state_t bt_state_mass[8];

	led_state_t ledState;
} stateStruct;



typedef struct {
	TCHAR audioFile[FILE_NAME_LEGHT];
	TCHAR icoFile[FILE_NAME_LEGHT];
} track_t;

typedef TCHAR file_t[FILE_NAME_LEGHT];

typedef struct {
	uint8_t WIFI_mode; 
	char * WIFI_ssid;
	char * WIFI_pass;
	uint8_t WIFI_channel;
	
	uint8_t DHCP;

	char *ipAdress;
	char *netMask;
	char *gateWay;

	char *device_name;

	char *FTP_login;
	char *FTP_pass;

	char *mqttBrokerAdress;
	char *mqttLogin;
	char *mqttPass;

	char *mqttTopic_phoneUp;
	char *mqttTopic_lifetime;
	
	uint8_t monofonEnable;
	uint8_t playerMode;
	uint8_t trackEnd_action;
	uint8_t phoneDown_action;
	uint16_t phoneUp_delay;
	uint8_t relay_inverse;

	int volume;

	int brightMax;
	int brightMin;
	RgbColor RGB;
	uint8_t animate;
	uint8_t rainbow;

	file_t soundTracks[MAX_NUM_OF_TRACKS];
	file_t trackIcons[MAX_NUM_OF_TRACKS];

	uint8_t touchSensInverted;
	uint8_t phoneSensInverted;

	uint8_t sensMode;
	uint8_t sensDebug;
	uint16_t magnitudeLevel;

	char configFile[FILE_NAME_LEGHT];
	char introIco[FILE_NAME_LEGHT];

	char ssidT[33];

} configuration;


uint8_t loadConfig(void);
FRESULT scan_in_dir(const char *file_extension, FF_DIR *dp, FILINFO *fno);
void load_Default_Config(void);
void writeErrorTxt(const char *buff);
uint8_t loadContent(void);
int saveConfig(void);

uint8_t scanFileSystem();
uint8_t scan_dir(const char *path);
