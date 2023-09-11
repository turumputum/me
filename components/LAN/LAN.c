#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_efuse.h"

//#include <sys/socket.h>
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
//#include "ethernet_init.h"

#include "stateConfig.h"

#include "LAN.h"
#include "ftp.h"
#include "myMqtt.h"
#include "tinyosc.h"
#include "reporter.h"
#include "executor.h"

#define CONFIG_ETH_SPI_ETHERNET_W5500 1

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
static const char *TAG = "LAN";

typedef struct {
	uint8_t spi_cs_gpio;
	uint8_t int_gpio;
	int8_t phy_reset_gpio;
	uint8_t phy_addr;
} spi_eth_module_config_t;

extern configuration me_config;
extern stateStruct me_state;
extern QueueHandle_t exec_mailbox;

static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	uint8_t mac_addr[6] = { 0 };
	/* we can get the ethernet driver handle from event data */
	esp_eth_handle_t eth_handle = *(esp_eth_handle_t*) event_data;

	switch (event_id) {
	case ETHERNET_EVENT_CONNECTED:
		esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
		ESP_LOGI(TAG, "Ethernet Link Up");
		ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
		break;
	case ETHERNET_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "Ethernet Link Down");
		break;
	case ETHERNET_EVENT_START:
		ESP_LOGI(TAG, "Ethernet Started");
		break;
	case ETHERNET_EVENT_STOP:
		ESP_LOGI(TAG, "Ethernet Stopped");
		break;
	default:
		break;
	}
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	ip_event_got_ip_t *event = (ip_event_got_ip_t*) event_data;
	const esp_netif_ip_info_t *ip_info = &event->ip_info;
	me_state.LAN_init_res = ESP_OK;

	ESP_LOGI(TAG, "Ethernet Got IP Address");
	ESP_LOGI(TAG, "~~~~~~~~~~~");
	ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
	ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
	ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
	ESP_LOGI(TAG, "~~~~~~~~~~~");
}

void osc_recive_task(){
	int buff_size=250;
	char buff[buff_size];

	tosc_message osc;

	struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
    socklen_t socklen = sizeof(source_addr);

	struct sockaddr_in dest_addr;
	dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	dest_addr.sin_family = AF_INET;
	dest_addr.sin_port = htons(me_config.oscMyPort);

	int err = bind(me_state.osc_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
	if (err < 0) {
		ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
	}

	ESP_LOGD(TAG, "OSC revive task STARTED, on port:%d",me_config.oscMyPort);
	while(1){
		int len=recvfrom(me_state.osc_socket, buff, buff_size-1, 0,(struct sockaddr *)&source_addr, &socklen);
		if(len<0){
			ESP_LOGD(TAG, "OSC incoming fail(");
		}else{
			ESP_LOGD(TAG, "OSC incoming:%s", buff);
			if (!tosc_parseMessage(&osc, buff, len)) {
				//printf("Received OSC message: [%i bytes] %s %s ",
					//len, // the number of bytes in the OSC message
					//tosc_getAddress(&osc), // the OSC address string, e.g. "/button1"
					tosc_getFormat(&osc); // the OSC format string, e.g. "f"
				for (int i = 0; osc.format[i] != '\0'; i++) {
					if(osc.format[i]== 'i'){
						//printf("%i ", ); break;
						exec_message_t message;
						memset(message.str,0,strlen(message.str));
						message.length = sprintf(message.str, "%s:%d", tosc_getAddress(&osc), tosc_getNextInt32(&osc));

						ESP_LOGD(TAG, "Add to exec_queue:%s ",message.str);
						if (xQueueSend(exec_mailbox, &message, portMAX_DELAY) != pdPASS) {
							ESP_LOGE(TAG, "Send message FAIL");
						}
					} 
				}
				printf("\n");
			}
		}
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

void wait_lan()
{
	while (me_state.LAN_init_res != ESP_OK)
	{
		vTaskDelay(pdMS_TO_TICKS(100));
	}

	// if(me_config.MDNS_enable){
	// 	mdns_start();
	// }
	if(me_config.FTP_enable){
		//TO-DO speed up needed
		xTaskCreatePinnedToCore(ftp_task, "FTP", 1024 * 10, NULL, configMAX_PRIORITIES - 5, NULL, 0);
	}
	if (strlen(me_config.mqttBrokerAdress) > 3)	{
		mqtt_app_start();
	}

	if((me_state.osc_socket >= 0)&&(me_config.oscMyPort>0)){
		xTaskCreate(osc_recive_task, "osc_recive_task", 1024 * 4, NULL, configMAX_PRIORITIES - 8, NULL);
	}
	vTaskDelete(NULL);
}

int LAN_init(void) {
	me_state.LAN_init_res = -1;

	// Initialize TCP/IP network interface (should be called only once in application)
	ESP_ERROR_CHECK(esp_netif_init());
	// Create default event loop that running in background
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	esp_netif_ip_info_t info_t;
	memset(&info_t, 0, sizeof(esp_netif_ip_info_t));

	esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();

	esp_netif_config_t cfg_spi = { .base = &esp_netif_config, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH };
	esp_netif_t *eth_netif_spi = NULL;
	esp_netif_config.ip_info = &info_t;
	esp_netif_config.if_key = "ETH_SPI_0";
	esp_netif_config.if_desc = "eth0";
	esp_netif_config.route_prio = 30 - 1;
	eth_netif_spi = esp_netif_new(&cfg_spi);



	// Init MAC and PHY configs to default
	eth_mac_config_t mac_config_spi = ETH_MAC_DEFAULT_CONFIG();
	eth_phy_config_t phy_config_spi = ETH_PHY_DEFAULT_CONFIG();

	// Install GPIO ISR handler to be able to service SPI Eth modlues interrupts
	gpio_install_isr_service(0);

	// Init SPI bus
	spi_device_handle_t spi_handle = { NULL };
	spi_bus_config_t buscfg = { .miso_io_num = 13, .mosi_io_num = 9, .sclk_io_num = 12, .quadwp_io_num = -1, .quadhd_io_num = -1, };
	ESP_ERROR_CHECK(spi_bus_initialize(1, &buscfg, SPI_DMA_CH_AUTO));

	// Init specific SPI Ethernet module configuration from Kconfig (CS GPIO, Interrupt GPIO, etc.)
	spi_eth_module_config_t spi_eth_module_config;
	spi_eth_module_config.spi_cs_gpio = 11;
	spi_eth_module_config.int_gpio = 16;
	spi_eth_module_config.phy_reset_gpio = 14;
	spi_eth_module_config.phy_addr = 1;

	esp_eth_mac_t *mac_spi;
	esp_eth_phy_t *phy_spi;
	esp_eth_handle_t eth_handle_spi = { NULL };

	spi_device_interface_config_t devcfg = { .command_bits = 16, // Actually it's the address phase in W5500 SPI frame
			.address_bits = 8,  // Actually it's the control phase in W5500 SPI frame
			.mode = 0, .clock_speed_hz = 20 * 1000 * 1000, .queue_size = 20 };

	devcfg.spics_io_num = spi_eth_module_config.spi_cs_gpio;

	ESP_ERROR_CHECK(spi_bus_add_device(1, &devcfg, &spi_handle));
	// w5500 ethernet driver is based on spi driver
	eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);

	// Set remaining GPIO numbers and configuration used by the SPI module
	w5500_config.int_gpio_num = spi_eth_module_config.int_gpio;
	phy_config_spi.phy_addr = spi_eth_module_config.phy_addr;
	phy_config_spi.reset_gpio_num = spi_eth_module_config.phy_reset_gpio;

	mac_spi = esp_eth_mac_new_w5500(&w5500_config, &mac_config_spi);
	phy_spi = esp_eth_phy_new_w5500(&phy_config_spi);

	esp_eth_config_t eth_config_spi = ETH_DEFAULT_CONFIG(mac_spi, phy_spi);
	ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config_spi, &eth_handle_spi));

	/* The SPI Ethernet module might not have a burned factory MAC address, we cat to set it manually.
	 02:00:00 is a Locally Administered OUI range so should not be used except when testing on a LAN under your control.
	 */

	//--TODO mac to id link--
	uint8_t chip_id[6];
	esp_efuse_mac_get_default(chip_id);
	ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle_spi, ETH_CMD_S_MAC_ADDR, chip_id));

	// attach Ethernet driver to TCP/IP stack
	ESP_ERROR_CHECK(esp_netif_attach(eth_netif_spi, esp_eth_new_netif_glue(eth_handle_spi)));
	ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

	ESP_ERROR_CHECK(esp_eth_start(eth_handle_spi));

	if (me_config.DHCP == 0) {
		ip4addr_aton((const char*) me_config.ipAdress, &info_t.ip);
		ip4addr_aton((const char*) me_config.gateWay, &info_t.gw);
		ip4addr_aton((const char*) me_config.netMask, &info_t.netmask);
		esp_netif_dhcpc_stop(eth_netif_spi);
		esp_netif_set_ip_info(eth_netif_spi, &info_t);
	}

	if(me_config.udpServerAdress[0]!=0){
		me_state.udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (me_state.udp_socket < 0) {
			printf("Failed to create socket for UDP: %d\n", errno);
			return -1;
		}else{
			ESP_LOGD(TAG,"UDP socket OK num:%d", me_state.udp_socket);
		}
		//TO DO create task to execute actions
		//struct sockaddr_in destAddr
		//int res = sendto(sock, msg, msgLen, 0, (struct sockaddr*)&destAddr, sizeof(destAddr));
		//"lwipopts.h"
	}

	if(me_config.oscServerAdress[0]!=0){
		me_state.osc_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (me_state.osc_socket < 0) {
			printf("Failed to create socket for OSC: %d\n", errno);
			return -1;
		}else{
			ESP_LOGD(TAG,"OSC socket OK num:%d", me_state.osc_socket);
		}

		struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt (me_state.osc_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

		//TO DO create task to execute actions
	}else{
		ESP_LOGD(TAG,"OSC skipped addr:%s", me_config.oscServerAdress);
	}

	xTaskCreate(wait_lan, "wait_lan", 1024 * 4, NULL, configMAX_PRIORITIES - 8, NULL);
	vTaskDelay(pdMS_TO_TICKS(1000));

	return 0;
}
