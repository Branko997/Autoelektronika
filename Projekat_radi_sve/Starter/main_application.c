/* Standard includes. */
#include <stdio.h>l.g 
#include <conio.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"

/* Hardware simulator utility functions */
#include "HW_access.h"

/* SERIAL SIMULATOR CHANNEL TO USE */
#define COM_CH (0)
#define COM_CH_1 (1)

	/* TASK PRIORITIES */
#define TASK_ALARM_PRI				( tskIDLE_PRIORITY + 1 )
#define	SERVICE_TASK_PRI			( tskIDLE_PRIORITY + 2 )
#define TASK_PC_COMMAND				( tskIDLE_PRIORITY + 3 )
#define TASK_SDH_PRI				( tskIDLE_PRIORITY + 4 )
#define TASK_SEG7_Task				( tskIDLE_PRIORITY + 5 )
#define	TASK_SERIAL_SEND_PRI		( tskIDLE_PRIORITY + 6 )
#define TASK_PC_SERIAL_REC			( tskIDLE_PRIORITY + 7 )
#define TASK_SERIAl_REC_PRI			( tskIDLE_PRIORITY + 8 )
#define LED_TASK_MANAGER_PRI        ( tskIDLE_PRIORITY + 9 )



/* TASKS: FORWARD DECLARATIONS */
void led_bar_tsk( void *pvParameters );
void SerialSend_Task(void* pvParameters);
void SerialReceive_Task(void* pvParameters);
void PC_SerialReceive_Task(void* pvParameters);
void SensorDataHandler(void* pvParameters);
void Seg7Task(void* pvParameters);
void AlarmTask(void* pvParameters);
void PC_command(void* pvParameters);
void Led_Manager_task(void* pvParametars);


/* TRASNMISSION DATA - CONSTANT IN THIS APPLICATION */
const char trigger[] = "XYZ";
unsigned volatile t_point;

/* RECEPTION DATA BUFFER */
#define R_BUF_SIZE (6)
uint8_t r_buffer[R_BUF_SIZE];
unsigned volatile r_point;

/* 7-SEG NUMBER DATABASE - ALL HEX DIGITS */
static const char hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 
								0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };

/*MESSAGES*/
/*state messages*/
#define MONITOR 'm'
#define DRIVE   'd'
#define SPEED   's'
#define OFF     'o'

/*led messages*/
#define ALARM_ON 0xff
#define LED_ON   0x01
#define LED_OFF  0x00

/*BUTTON MASKS*/
#define BTN1 0x04
#define BTN2 0x02
#define BTN3 0x01
#define NO_BTN 0x00

/*CRITICAL VALUES*/
#define MAX_COOLANT_TEMP  110u
#define MAX_REVS		 6000u


/*DATA STRUCTS*/
typedef struct sensor_val{/*struct for holding sensor values*/
	uint8_t air_temp;
	uint8_t coolant_temp;
	uint16_t revs;
	uint8_t manifold_air_press;
	uint8_t gas_pedal_pos;
}sensor_val;

typedef struct commands {
	uint8_t word[7];
	uint8_t length;
}commands;


/* GLOBAL OS-HANDLES */
SemaphoreHandle_t LED_INT_BinarySemaphore;
SemaphoreHandle_t TBE_BinarySemaphore;
SemaphoreHandle_t RXC_BinarySemaphore;
SemaphoreHandle_t RXC_PCSemaphore;
SemaphoreHandle_t AlarmStateSem;
TimerHandle_t per_TimerHandle;
QueueHandle_t SensorQueue;
QueueHandle_t MessageQueue;
QueueHandle_t Seg7Queue;
QueueHandle_t LedQueue;
QueueHandle_t PCcommand;
QueueHandle_t LedBarQueue;


/*Local function declarations*/
/*Function used to format sensor data and current mode code*/
void FormAndSend7SegData(sensor_val SensTemp, uint8_t Msg);
void LedManager(sensor_val SensTemp, uint8_t Msg);

/* OPC - ON INPUT CHANGE - INTERRUPT HANDLER */
static uint32_t OnLED_ChangeInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(LED_INT_BinarySemaphore, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);
}

/* TBE - TRANSMISSION BUFFER EMPTY - INTERRUPT HANDLER */
static uint32_t prvProcessTBEInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(TBE_BinarySemaphore, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);
}

/* RXC - RECEPTION COMPLETE - INTERRUPT HANDLER */
static uint32_t prvProcessRXCInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	if (get_RXC_status(0) != 0)
		xSemaphoreGiveFromISR(RXC_BinarySemaphore, &xHigherPTW);

	if (get_RXC_status(1) != 0)
		xSemaphoreGiveFromISR(RXC_PCSemaphore, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);
}

/* PERIODIC TIMER CALLBACK */
static void TimerCallback(TimerHandle_t xTimer)
{ 
	xSemaphoreGive(TBE_BinarySemaphore); 
	xSemaphoreGive(AlarmStateSem);
}


/* MAIN - SYSTEM STARTUP POINT */
void main_demo( void )
{
	init_7seg_comm();
	init_LED_comm();
	init_serial_uplink(COM_CH);  // inicijalizacija serijske TX na kanalu 0
	init_serial_downlink(COM_CH);// inicijalizacija serijske TX na kanalu 0

	init_serial_uplink(COM_CH_1);
	init_serial_downlink(COM_CH_1);

	/* QUEUES  */
	SensorQueue = xQueueCreate(1u, sizeof(sensor_val));
	MessageQueue = xQueueCreate(5u, sizeof(uint8_t));
	Seg7Queue = xQueueCreate(10u, sizeof(uint8_t));
	LedQueue = xQueueCreate(1u, sizeof(uint8_t));
	PCcommand = xQueueCreate(1u, sizeof(commands));
	LedBarQueue = xQueueCreate(10u, sizeof(uint8_t));


	/* ON INPUT CHANGE INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_OIC, OnLED_ChangeInterrupt);

	/* Create LED interrupt semaphore */
	LED_INT_BinarySemaphore = xSemaphoreCreateBinary();

	/* create a timer task */
	per_TimerHandle = xTimerCreate("Timer", pdMS_TO_TICKS(100), pdTRUE, NULL, TimerCallback);
	xTimerStart(per_TimerHandle, 0);

	/*Create alarm task*/
	xTaskCreate(AlarmTask, "ALRM", configMINIMAL_STACK_SIZE, NULL, TASK_ALARM_PRI, NULL);

	/*Create sensor data handler task*/
	xTaskCreate(SensorDataHandler, "Sdh", configMINIMAL_STACK_SIZE, NULL, TASK_SDH_PRI, NULL);

	/*Create 7 segment driver task*/
	xTaskCreate(Seg7Task, "Se7", configMINIMAL_STACK_SIZE, NULL, TASK_SEG7_Task, NULL);
	
	/* SERIAL TRANSMITTER TASK */
	xTaskCreate(SerialSend_Task, "STx", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_SEND_PRI, NULL);

	/* SERIAL RECEIVER TASKS */
	xTaskCreate(SerialReceive_Task, "SRx", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI, NULL);
	r_point = 0;
	xTaskCreate(PC_SerialReceive_Task, "PCRx", configMINIMAL_STACK_SIZE, NULL, TASK_PC_SERIAL_REC, NULL);

	/*Create a task that handles pc command*/
	xTaskCreate(PC_command, "PCCx", configMINIMAL_STACK_SIZE, NULL, TASK_PC_COMMAND, NULL);

	xTaskCreate(Led_Manager_task, "lmt", configMINIMAL_STACK_SIZE, NULL, LED_TASK_MANAGER_PRI, NULL);

	/* Create TBE semaphore - serial transmit comm */
	TBE_BinarySemaphore = xSemaphoreCreateBinary();

	/* Create TBE semaphore - serial receive comm */
	RXC_BinarySemaphore = xSemaphoreCreateBinary();
	RXC_PCSemaphore = xSemaphoreCreateBinary();

	/*Create State Semaphores*/
	AlarmStateSem = xSemaphoreCreateBinary(); 
	
	/* SERIAL TRANSMISSION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_TBE, prvProcessTBEInterrupt);

	/* SERIAL RECEPTION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt);

	/* create a led bar TASK */
	xTaskCreate(led_bar_tsk, "ST",	configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);

	vTaskStartScheduler();

	while (1);
}

/*This task checks if sensor values are within normal operating range,*/
/*then sends sensor data to their state queues, determines led output state also*/
void SensorDataHandler(void* pvParameters)
{
	
	sensor_val SensTemp; /*buffer for item from queue*/
	uint8_t led = LED_OFF;/*Holds led value, initialised as turned off*/
	uint8_t Msg = OFF;/*Hold state value, initialised as system turned off*/
	while (1)
	{	
		xQueueReceive(SensorQueue, &SensTemp, portMAX_DELAY);/*recieve new sensor data*/
		/*check if new mode command has arrived, if not use old mode*/
		if (uxQueueMessagesWaiting(MessageQueue)) {
			xQueueReceive(MessageQueue, &Msg, portMAX_DELAY);
		}
	    LedManager(SensTemp, Msg);
		FormAndSend7SegData(SensTemp, Msg);/*format mode and sensor data for 7 segment display*/
		/*Check how should the led display work and send data to alarm task*/
		if ((MONITOR == Msg)||(DRIVE == Msg)||(SPEED == Msg)) {/*if we are in working mode*/
			if (MAX_COOLANT_TEMP < SensTemp.coolant_temp)
			{/*check if engine is overheating, turn on alarm if needed*/
				led = ALARM_ON;
			}
			else if (MAX_REVS < SensTemp.revs)
			{/*check if engine revs are too high, turn on alarm if needed*/
				led = ALARM_ON;
			}
			else/*all ok, turn on power on led*/
			{
				led = LED_ON;
			}
		}
		else/*if the system is turned off or a bad message is sent turn off display*/
		{
			led = LED_OFF;
		}
		xQueueSend(LedQueue, &led, portMAX_DELAY);/*send data to alarm task*/
	}
}


/*Save values gotten from led inputs (buttons), and send message for mode change*/
void led_bar_tsk(void* pvParameters)
{
	//unsigned i;
	uint8_t d;
	uint8_t mode;
	while (1)
	{  
		/*wait until a interrupt on led change gives a semaphore*/
		/*read the value and send a corresponding message*/
		xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);
		get_LED_BAR(0, &d);
		switch (d) 
		{
		case BTN1:/*first button is monitor mode*/
			mode = MONITOR;
			break;
		case BTN2:/*second button is drive mode*/
			mode = DRIVE;
			break;
		case BTN3:/*third button is speed mode*/
			mode = SPEED;
			break;
		case NO_BTN:/*when all buttons are released turn off system*/
			mode = OFF;
			break;
		default:/*if random button combination is pressed turn off system and wait for new command*/
			mode = OFF;
			break;
		}
		xQueueSend(MessageQueue, &mode, portMAX_DELAY);
	}
}

/*Write values to 7 segment display*/
void Seg7Task(void* pvParameters) 
{
	uint8_t SegNum;
	int i;
	while (1)
	{/*Send queue items to 7seg display*/
		for (i = 0; i < 1; i++) {

			xQueueReceive(Seg7Queue, &SegNum, portMAX_DELAY);
			select_7seg_digit(i);
			set_7seg_digit(hexnum[SegNum]);
		}
	}


}

/*This task implements LED alarm display */
void AlarmTask(void* pvParameters)
{
	uint8_t led = LED_OFF;
	uint8_t temp_led;
	while (1)
	{	
		xSemaphoreTake(AlarmStateSem, portMAX_DELAY);/*needs to be here for 200ms period of led blink*/
		temp_led = led;/*remember old value for comparison*/
		xQueueReceive(LedQueue, &led, portMAX_DELAY);/*load new value*/
		if ((temp_led == led) && (ALARM_ON == led)) 
		{/*if this is a even entry while alarm is on turn off all leds*/
			led = LED_OFF;
			set_LED_BAR(1, led);
		}
		else /*if its an odd entry while alarm is on turn on all leds, if not alarm is off do as requested*/
		{
			set_LED_BAR(1, led);
		}
	}
}

/*This task implements LED display for display sensor values */
void Led_Manager_task(void* pvParametars) 
{
	uint8_t led_data = LED_OFF;
	uint8_t temp_data = 0xff;
	while(1) {
		for (uint8_t i = 0; i < 2; i++) {
			temp_data = led_data;
			xQueueReceive(LedBarQueue, &led_data, portMAX_DELAY);
				set_LED_BAR(i+2, led_data);
		}

	}

}


void PC_command(void* pvParameters)
{
	uint8_t mode = OFF;
	commands command;
	while (1)
	{
		xQueueReceive(PCcommand, &command, portMAX_DELAY);
		if (0u != command.length) {/*if there was no error handle commands*/
			if (0u == strncmp(command.word, "MONITOR", command.length)) 
			{
				mode = MONITOR;
			}
			else if (0u == strncmp(command.word, "DRIVE", command.length))
			{
				mode = DRIVE;
			}
			else if (0u == strncmp(command.word, "SPEED", command.length))
			{
				mode = SPEED;
			}
			else 
			{/*If anything elese is sent turn off system including OFF command*/
				mode = OFF;
			}
			/*Send message to sensor data handler to know which mode is on*/
			xQueueSend(MessageQueue, &mode, 0u);
			//printf("mode = %c \n", mode);
		}
		/*Dont inform messages was bad because it was done in PC_SerialReceive_Task*/
	}
}

/*Task for receiving pc commands and sending them to another task to handle them*/
void PC_SerialReceive_Task(void* pvParameters) 
{
	uint8_t cc;
	commands temp;
	uint8_t pos = 0;
	uint8_t Msg = OFF;
	while (1)
	{
		xSemaphoreTake(RXC_PCSemaphore, portMAX_DELAY);/*suspend task until a character is received*/
		get_serial_character(COM_CH_1, &cc);
		if (0x0d == cc)/*if CR received send data*/
		{
			xQueueOverwrite(PCcommand, &temp);/*send command*/
			pos = 0u;/*restart position for next command*/
		}
		else/*not the end of command, add character to buffer*/
		{
			if (6u >= pos) {/*if command contains more than 7 characters it is badly sent, handle in else*/
				temp.word[pos] = cc;/*add character to array*/
				temp.length = pos + 1u;/*save position*/
				pos++;/*increment for next character*/
			}
			else
			{/*max word size exceeded, turn off system probably error*/
				xQueueSend(MessageQueue, &Msg, 0u);
				temp.length = 0u;/*word size 0 so we know there was an error when CR is received*/
			}
			
		}
	}
}

/*Task is used for polling sensor serial line*/
void SerialSend_Task(void* pvParameters)
{
	t_point = 0;
	while (1)
	{
		if (t_point > (sizeof(trigger) - 1))/*if we sent whole trigger word, reset trigger position*/
		{
			t_point = 0;
		}
		if (0u == t_point)/*if next is first char of the trigger block task*/
		{
			xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);/*block task until 100ms have passed*/
		}
		send_serial_character(COM_CH, trigger[t_point++]); /*send trigger for auto transmision*/
		xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);/*block task until transmit buffer is empty*/	
	}
}


/*Task handles receiving sensor data and then puts it in a queue*/
void SerialReceive_Task(void* pvParameters)
{
	uint8_t cc = 0;
	uint8_t Msg = OFF;
	static uint8_t loca = 0;
	sensor_val SensTemp;
	while (1)
	{
		xSemaphoreTake(RXC_BinarySemaphore, portMAX_DELAY);/*suspend task until a character is received*/
		get_serial_character(COM_CH, &cc);
		
		if ((0x00 == cc)&&(R_BUF_SIZE == r_point)) /*initialise recieve buffer*/
		{/*second check is if some sensor values are 0x00, so that we don't reinitialise the buffer */
			r_point = 0;			
		}
		else if ((cc == 0xff)&&(R_BUF_SIZE == r_point))/*end character case*/
		{/*second check is if some sensor values are 0xff, so that we don't finish earlier*/
			/*load SensTemp with r_buffer*/
			SensTemp.air_temp = r_buffer[0];
			SensTemp.coolant_temp = r_buffer[1];
			SensTemp.revs = ((uint16_t)r_buffer[2]<<8u)|(uint16_t)r_buffer[3];
			SensTemp.manifold_air_press = r_buffer[4];
			SensTemp.gas_pedal_pos = r_buffer[5];
			xQueueSend(SensorQueue, &SensTemp, 0U);/*pass r_buffer to queue*/
		}
		else if (r_point < R_BUF_SIZE)// pamti karaktere izmedju 0 i FF
		{
			r_buffer[r_point++] = cc;
		}
		else/*comm error case turn off system*/
		{
			xQueueSend(MessageQueue, &Msg, 0U);
		}
	}
}

/*This function formats data for displaying based on the current mode*/
/*It is called every time new sensor data is available*/
void FormAndSend7SegData(sensor_val SensTemp, uint8_t Msg)
{

	uint8_t Seg_data = 0;
	int i=0;
	switch (Msg)
	{
	case MONITOR:/*add monitor values to Seg_data array*/
		Seg_data = 1;
		break;
	case DRIVE:/*add drive values to Seg_data array*/
		Seg_data = 2;
		break;
	case SPEED:/*add speed values to Seg_data array*/
		Seg_data = 3;
		break;
	default:
		/*last option to be sent is off state so we wont write anything to 7seg displey*/
		/*if unknown value is sent system is off*/
		break;
	}
	/*send array values to a queue to be displayed on 7 segment display*/
	
	
		xQueueSend(Seg7Queue, &Seg_data, portMAX_DELAY);
	
}

/*This function formats data for displaying based on the current mode*/

void LedManager(sensor_val SensTemp, uint8_t Msg) {

	uint8_t led_data[2] = {0, 0};
	int i;
	int mc = 32;
	int vc = 800;

	uint8_t arr[8] = { 0x00, 0x01, 0x03, 0x07, 0x1f, 0x3f, 0x7f, 0xff };

	switch (Msg)
	{
	case MONITOR:
		for (i = 1; i < 9; i++) {
			if (SensTemp.coolant_temp < i * mc) {
				led_data[0] = arr[i - 1];
				i = 9;
			}
		}
		for (i = 1; i < 9; i++) {
			if (SensTemp.air_temp < i * mc) {
				led_data[1] = arr[i - 1];
				i = 9;
			}
		}
		
		break;
	case DRIVE:
		for (i = 1; i < 9; i++) {
			if (SensTemp.revs < i * vc) {
				led_data[0] = arr[i - 1];
				i = 9;
			}
			else
			{
				led_data[0] = arr[7];
			}
		}
		for (i = 1; i < 9; i++) {
			if (SensTemp.manifold_air_press < i * mc) {
				led_data[1] = arr[i - 1];
				i = 9;
			}
		}

		break;
	case SPEED:
		for (i = 1; i < 9; i++) {
			if (SensTemp.revs < i * vc) {
				led_data[0] = arr[i - 1];
				i = 9;
			}
			else
			{
				led_data[0] = arr[7];
			}
		}
		for (i = 1; i < 9; i++) {
			if (SensTemp.gas_pedal_pos < i * mc) {
				led_data[1] = arr[i - 1];
				i = 9;
			}
		}
		
		break;
	default:

		break;
	}

	/*send array values to a queues to be displayed on led bar*/
	for (i = 0; i < 2; i++) {
		xQueueSend(LedBarQueue, &led_data[i], portMAX_DELAY);
	}
}