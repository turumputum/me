/*
 * cdc.h
 *
 *  Created on: 7 ���. 2022 �.
 *      Author: Yac
 */

#ifndef MAIN_CDC_H_
#define MAIN_CDC_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#define CDC_STACK_SZIE      (configMINIMAL_STACK_SIZE*12)

StackType_t cdc_stack[CDC_STACK_SZIE];
StaticTask_t cdc_taskdef;

void usb_device_task(void *param);
void usbprintf(char * msg, ...);
void usbprint(char * msg);
void usb_device_task(void *param);
void cdc_task(void *params);


#endif /* MAIN_CDC_H_ */
