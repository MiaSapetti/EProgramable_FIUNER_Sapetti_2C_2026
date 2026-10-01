/**
 * @file guia2_act4.c
 * @mainpage Conversión Digital-Analógica (ECG) y Conversión Analógica-Digital enviada por UART.
 *
 * Este programa genera una señal analógica de ECG a través del periférico DAC leyendo 
 * un vector digital en memoria, y simultáneamente realiza lecturas analógicas periódicas 
 * mediante el ADC (canal CH1) enviando el resultado vía UART para ser visualizado en un osciloscopio o terminal serie.
 *
 * @section hardConn Conexión de Hardware
 *
 * | Periférico | ESP32 / EDU-CIAA |
 * |:----------:|:-----------------|
 * |  Salida DAC | Terminal A del potenciómetro |
 * |  Entrada AD | CH1 (Cursor/Centro del potenciómetro) |
 * |  GND        | Terminal B del potenciómetro |
 * |  UART PC    | USB / Serial |
 *
 * @section changelog Historial de Cambios
 *
 * | Fecha      | Descripción |
 * |:----------:|:------------|
 * | 24/09/2026 | Creación del documento |
 * | 01/10/2026 | Documentación Doxygen y adaptación para prueba con ECG |
 *
 * @author Mia Sapetti (mia.sapetti@ingenieria.uner.edu.ar)
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
/** @brief Período del temporizador para la tarea conversora ADC (2000 us -> Frecuencia 500 Hz) */
#define CONFIG_BLINK_PERIOD_ADC_US 2000 //periodo de muestreo de 2ms para frecuencia de 500 Hz, para el timer de la tarea conversora
/** @brief Período del temporizador para la tarea conversora y salida DAC (4000 us -> Frecuencia 250 Hz) */
#define CONFIG_PERIOD_DAC_US       4000 // Salida DAC (frecuencia sugerida para ECG 250 Hz, mitad que la de muestreo)
/** @brief Tamaños del buffer de muestras de la señal ECG */
#define BUFFER_SIZE 231

/*==================[internal data definition]===============================*/
/** @brief Handle de la tarea encargada de la conversión analógico-digital */
TaskHandle_t Conversor_AD_task_handle = NULL;

/** @brief Handle de la tarea encargada de la conversión digital-analógica */
TaskHandle_t Conversor_DA_task_handle = NULL;

/** @brief Handle de la tarea principal */
TaskHandle_t main_task_handle = NULL;
/*==================[internal functions declaration]=========================*/
/**
 * @brief Función de interrupción asociada al Timer A para notificar a la tarea de conversión ADC.
 * @param param Parámetro genérico (sin uso).
 */
void FuncTimerA(void* param){
    vTaskNotifyGiveFromISR(Conversor_AD_task_handle, pdFALSE);
}

/**
 * @brief Función de interrupción asociada al Timer B para notificar a la tarea de conversión DAC.
 * @param param Parámetro genérico (sin uso).
 */
void FuncTimerB(void* param){
    vTaskNotifyGiveFromISR(Conversor_DA_task_handle, pdFALSE);
}

/**
 * @brief Buffer digital con los puntos de muestra de un ciclo de la señal de ECG.
 */
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

/**
 * @brief Tarea encargada de enviar progresivamente cada muestra del buffer ECG al convertidor Digital-Analógico (DAC).
 * @param pvParameter Parámetro de tarea de FreeRTOS (no utilizado).
 */
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

/**
 * @brief Tarea encargada de leer el canal analógico CH1 y transmitir la medición vía UART hacia la PC.
 * @param pvParameter Parámetro de tarea de FreeRTOS (no utilizado).
 */
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
/**
 * @brief Función principal de entrada de la aplicación.
 */
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
        .input   = CH1,
        .mode    = ADC_SINGLE,
        .func_p  = NULL,
        .param_p = NULL,
    };
    AnalogInputInit(&conv_AD);

    // 3. Inicialización de la Salida Analógica (DAC)
    AnalogOutputInit();

    // 4. Configuración del Timer B para la generación DAC
    timer_config_t timer_ConversorDA = {
        .timer   = TIMER_B,
        .period  = CONFIG_PERIOD_DAC_US,
        .func_p  = FuncTimerB,
        .param_p = NULL
    };
    TimerInit(&timer_ConversorDA);

    // 5. Configuración del Timer A para el muestreo ADC
    timer_config_t timer_ConversorAD = {
        .timer   = TIMER_A,
        .period  = CONFIG_BLINK_PERIOD_ADC_US,
        .func_p  = FuncTimerA,
        .param_p = NULL
    };
    TimerInit(&timer_ConversorAD);

    // 6. Creación de las tareas de FreeRTOS
    xTaskCreate(&ConversorADTask, "ConversorAD", 4096, NULL, 5, &Conversor_AD_task_handle);
    xTaskCreate(&ConversorDATask, "ConversorDAC", 4096, NULL, 5, &Conversor_DA_task_handle);

    // 7. Inicio de los temporizadores
    TimerStart(timer_ConversorAD.timer);
    TimerStart(timer_ConversorDA.timer);
}
/*==================[end of file]============================================*/