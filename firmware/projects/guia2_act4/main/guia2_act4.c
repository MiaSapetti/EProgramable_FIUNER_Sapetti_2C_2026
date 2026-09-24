/*! @mainpage Template
 *
 * @section genDesc General Description
 *
 * This section describes how the program works.
 *
 * <a href="https://drive.google.com/...">Operation Example</a>
 *
 * @section hardConn Hardware Connection
 *
 * |    Peripheral  |   ESP32   	|
 * |:--------------:|:--------------|
 * | 	PIN_X	 	| 	GPIO_X		|
 *
 *
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 12/09/2023 | Document creation		                         |
 *
 * @author Albano Peñalva (albano.penalva@uner.edu.ar)
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "switch.h"
#include "lcditse0803.h"
#include "hc_sr04.h"
#include "timer_mcu.h"
#include "uart_mcu.h"
#include "analog_io_mcu.h"
/*==================[macros and definitions]=================================*/
#define CONFIG_BLINK_PERIOD_ADC_US 2000 //periodo de muestreo de 2ms para frecuencia de 500 Hz, para el timer de la tarea conversora
#define CONFIG_PERIOD_DAC_US       4000 // Salida DAC (frecuencia sugerida para ECG 250 Hz, mitad que la de muestreo)

#define BUFFER_SIZE 231

/*==================[internal data definition]===============================*/
TaskHandle_t Conversor_AD_task_handle   = NULL;
TaskHandle_t Conversor_DA_task_handle   = NULL;

/*==================[internal functions declaration]=========================*/
void FuncTimerA(void* param){
    vTaskNotifyGiveFromISR(Conversor_AD_task_handle, pdFALSE);
}

void FuncTimerB(void* param){
    vTaskNotifyGiveFromISR(Conversor_DA_task_handle, pdFALSE);
}
TaskHandle_t main_task_handle = NULL;

const char ecg[BUFFER_SIZE] = {
    76, 77, 78, 77, 79, 86, 81, 76, 84, 93, 85, 80,
    89, 95, 89, 85, 93, 98, 94, 88, 98, 105, 96, 91,
    99, 105, 101, 96, 102, 106, 101, 96, 100, 107, 101,
    94, 100, 104, 100, 91, 99, 103, 98, 91, 96, 105, 95,
    88, 95, 100, 94, 85, 93, 99, 92, 84, 91, 96, 87, 80,
    83, 92, 86, 78, 84, 89, 79, 73, 81, 83, 78, 70, 80, 82,
    79, 69, 80, 82, 81, 70, 75, 81, 77, 74, 79, 83, 82, 72,
    80, 87, 79, 76, 85, 95, 87, 81, 88, 93, 88, 84, 87, 94,
    86, 82, 85, 94, 85, 82, 85, 95, 86, 83, 92, 99, 91, 88,
    94, 98, 95, 90, 97, 105, 104, 94, 98, 114, 117, 124, 144,
    180, 210, 236, 253, 227, 171, 99, 49, 34, 29, 43, 69, 89,
    89, 90, 98, 107, 104, 98, 104, 110, 102, 98, 103, 111, 101,
    94, 103, 108, 102, 95, 97, 106, 100, 92, 101, 103, 100, 94, 98,
    103, 96, 90, 98, 103, 97, 90, 99, 104, 95, 90, 99, 104, 100, 93,
    100, 106, 101, 93, 101, 105, 103, 96, 105, 112, 105, 99, 103, 108,
    99, 96, 102, 106, 99, 90, 92, 100, 87, 80, 82, 88, 77, 69, 75, 79,
    74, 67, 71, 78, 72, 67, 73, 81, 77, 71, 75, 84, 79, 77, 77, 76, 76,
};

// Tarea 1: Genera la señal analógica de ECG enviando datos al DAC
static void ConversorDATask(void *pvParameter){
    uint8_t i = 0;
    while(true){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        // Escribe el valor digital en el DAC
        AnalogOutputWrite(ecg[i]);
        
        i++;
        if(i >= BUFFER_SIZE){
            i = 0; // Reinicia el ciclo del ECG
        }
    }
}

static void ConversorADTask(void *pvParameter){
    uint16_t valor_analogico = 0; //valor convertido por el ADC, se actualiza en la ISR del timer de la tarea conversora

	while(true){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Lectura del canal CH1 en milivoltios o cuentas según requiera el driver
        AnalogInputReadSingle(CH1, &valor_analogico);

        // Envío en formato para Serial Plotter: >voltaje:VALOR\r\n
        //UartSendString(UART_PC, ">voltaje:");
        UartSendString(UART_PC, (char *)UartItoa(valor_analogico, 10));
        UartSendString(UART_PC, "\r\n");
    }
}

/*==================[external functions definition]==========================*/
void app_main(void){
    // 1. Inicialización de la UART
	 serial_config_t my_uart = {
        .port      = UART_PC,
        .baud_rate = 115200,
        .func_p    = NULL,
        .param_p   = NULL
    };
    UartInit(&my_uart);

	// 2. Inicialización de la Entrada Analógica CH1
	analog_input_config_t conv_AD = {
		.input = CH1,
		.mode  = ADC_SINGLE,
		.func_p = NULL,
		.param_p = NULL,
	};
    AnalogInputInit(&conv_AD);

	AnalogOutputInit();

	timer_config_t timer_ConversorDA = {
		.timer   = TIMER_B,
		.period  = CONFIG_PERIOD_DAC_US,
		.func_p  = FuncTimerB,
		.param_p = NULL
	};
	TimerInit(&timer_ConversorDA);

	// 3. Inicialización del Timer A a 500 Hz (2000 us)
	timer_config_t timer_ConversorAD = {
		.timer   = TIMER_A,
		.period  = CONFIG_BLINK_PERIOD_ADC_US,
		.func_p  = FuncTimerA,
		.param_p = NULL
	};
	TimerInit(&timer_ConversorAD);

	// 4. Creación de la tarea
	xTaskCreate(&ConversorADTask, "ConversorAD",4096, NULL, 5, &Conversor_AD_task_handle);
	xTaskCreate(&ConversorDATask, "ConversorDAC",4096, NULL, 5, &Conversor_DA_task_handle);

    // 5. Iniciar Timer
	TimerStart(timer_ConversorAD.timer);
	TimerStart(timer_ConversorDA.timer);
}
/*==================[end of file]============================================*/