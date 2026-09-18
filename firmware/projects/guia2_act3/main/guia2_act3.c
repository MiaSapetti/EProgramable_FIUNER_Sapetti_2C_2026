/**
 * @file guia2_act3.c
 * @mainpage Medidor de Distancia por Ultrasonido con Interrupciones y Puerto Serie
 *
 * @section genDesc Descripción General
 *
 * Medición de distancia utilizando un sensor ultrasónico HC-SR04, visualización 
 * de la lectura en un display LCD BCD y representación del rango mediante LEDs, 
 * gestionado a través de timers e interrupciones de teclas en FreeRTOS.
 * Transmite la información hacia la PC vía Puerto Serie (UART_PC) y permite el
 * control remoto mediante comandos serie ('O', 'H', 'I', 'M', 'F', 'S').
 * 
 * @section changelog Historial de Cambios
 *
 * |    Fecha   | Descripción                                           |
 * |:----------:|:------------------------------------------------------|
 * | 17/09/2026 | Integración de UART, cambio de unidades y control     |
 * |            | de velocidad/máximo.                                  |
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

/*==================[macros and definitions]=================================*/

/**
 * @def CONFIG_BLINK_PERIOD_Medir_US
 * @brief Período inicial de disparo del Timer A para la tarea de medición (en microsegundos).
 */
#define CONFIG_BLINK_PERIOD_Medir_US 1000000 

/**
 * @def CONFIG_BLINK_PERIOD_Mostrar_US
 * @brief Período de disparo del Timer B para la tarea de visualización (en microsegundos).
 */
#define CONFIG_BLINK_PERIOD_Mostrar_US 1000000

/**
 * @def PERIOD_MIN_MEDIR_US
 * @brief Límite mínimo para el período del Timer A (100 ms).
 */
#define PERIOD_MIN_MEDIR_US  100000  

/**
 * @def PERIOD_MAX_MEDIR_US
 * @brief Límite máximo para el período del Timer A (3 s).
 */
#define PERIOD_MAX_MEDIR_US  3000000 

/**
 * @def STEP_PERIOD_US
 * @brief Paso de ajuste para el período del Timer A al cambiar la velocidad (100 ms).
 */
#define STEP_PERIOD_US        100000  

/*==================[internal data definition]===============================*/

/**
 * @brief Handle de la tarea encargada de la medición de distancia.
 */
TaskHandle_t Medir_task_handle   = NULL;

/**
 * @brief Handle de la tarea encargada de la visualización en LEDs, display LCD y Puerto Serie.
 */
TaskHandle_t Mostrar_task_handle = NULL;

/**
 * @brief Variable global que almacena el valor actual de la distancia medida.
 */
uint16_t distancia     = 0;

/**
 * @brief Variable global que almacena el valor congelado o activo a mostrar en el display LCD.
 */
uint16_t distancia_lcd = 0;

/**
 * @brief Flag global para encender/apagar el sistema de medición y muestra.
 *        - `true`: Sistema activo.
 *        - `false`: Sistema apagado.
 */
bool on   = true;

/**
 * @brief Flag global para congelar el valor medido en la pantalla LCD.
 *        - `true`: Mantiene congelado el último valor registrado.
 *        - `false`: Actualiza la pantalla dinámicamente.
 */
bool hold = false;

/**
 * @brief Flag global para seleccionar la unidad de medida.
 *        - `true`: Centímetros ("cm").
 *        - `false`: Pulgadas ("inch").
 */
bool unidades_cm = true;

/**
 * @brief Variable global que almacena el valor máximo registrado durante la sesión.
 */
uint16_t max_distancia = 0;

/**
 * @brief Flag global para alternar la visualización del valor máximo en pantalla.
 *        - `true`: Mantiene en pantalla el valor máximo alcanzado.
 *        - `false`: Muestra la medición actual.
 */
bool modo_maximo = false;

/**
 * @brief Variable global que almacena el período actual de la tarea de medición (en us).
 */
uint32_t periodo_medir_us = 1000000;

/*==================[internal functions declaration]=========================*/

/**
 * @brief Rutina de servicio de interrupción (ISR) invocada por el Timer A.
 * 
 * Envía una notificación de tarea a `MedirTask` para destrabar su ejecución.
 * 
 * @param[in] param Puntero genérico a parámetros de interrupción (no utilizado).
 */
void FuncTimerA(void* param){
    vTaskNotifyGiveFromISR(Medir_task_handle, pdFALSE);
}

/**
 * @brief Rutina de servicio de interrupción (ISR) invocada por el Timer B.
 * 
 * Envía una notificación de tarea a `MostrarTask` para destrabar su ejecución.
 * 
 * @param[in] param Puntero genérico a parámetros de interrupción (no utilizado).
 */
void FuncTimerB(void* param){
    vTaskNotifyGiveFromISR(Mostrar_task_handle, pdFALSE);
}

/**
 * @brief Callback de interrupción del Puerto Serie (UART_PC).
 * 
 * Se ejecuta al recibir un dato por el puerto serie. Realiza un eco del caracter recibido
 * y procesa los siguientes comandos:
 * - 'O' / 'o': Alterna el estado de encendido (@ref on).
 * - 'H' / 'h': Alterna el estado de retención (@ref hold).
 * - 'I' / 'i': Alterna la unidad de medida (@ref unidades_cm).
 * - 'M' / 'm': Alterna la visualización del valor máximo (@ref modo_maximo).
 * - 'F' / 'f': Incrementa la velocidad de lectura disminuyendo el período del Timer A.
 * - 'S' / 's': Reduce la velocidad de lectura aumentando el período del Timer A.
 * 
 * @param[in] param Puntero genérico a parámetros de interrupción (no utilizado).
 */
void FuncUART(void* param){ 
    uint8_t tecla;
    UartReadByte(UART_PC, &tecla);
    UartSendByte(UART_PC, (char *) &tecla); 

    if(tecla == 'O' || tecla == 'o'){
        on = !on;
    }
    else if(tecla == 'H' || tecla == 'h'){
        hold = !hold;
    }
    else if(tecla == 'I' || tecla == 'i'){ 
        unidades_cm = !unidades_cm;
    }
    else if(tecla == 'M' || tecla == 'm'){ 
        modo_maximo = !modo_maximo;
    }
    else if(tecla == 'F' || tecla == 'f'){
        if(periodo_medir_us > PERIOD_MIN_MEDIR_US){
            periodo_medir_us -= STEP_PERIOD_US;
            TimerUpdatePeriod(TIMER_A, periodo_medir_us);
            UartSendString(UART_PC, "Velocidad aumentada\r\n");
        }
    }
    else if(tecla == 'S' || tecla == 's'){
        if(periodo_medir_us < PERIOD_MAX_MEDIR_US){
            periodo_medir_us += STEP_PERIOD_US;
            TimerUpdatePeriod(TIMER_A, periodo_medir_us);
            UartSendString(UART_PC, "Velocidad disminuida\r\n");
        }
    }
}

/**
 * @brief Tarea de FreeRTOS encargada del control de LEDs, display BCD y transmisión UART.
 * 
 * Espera la notificación de la ISR del Timer B. Si el sistema está encendido (@ref on),
 * enciende los LEDs según la distancia, gestiona la selección del dato a mostrar
 * (respetando @ref modo_maximo y @ref hold), actualiza el display BCD y transmite
 * la información formateada a la PC a través de UART_PC.
 * Si @ref on es `false`, apaga el display y todos los LEDs.
 * 
 * @param[in] pvParameter Puntero a parámetros de tarea (no utilizado).
 */
static void MostrarTask(void *pvParameter){
    LcdItsE0803Init();

    while(true){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); 
        if(on){
            // 1. CONTROL DE LEDS (Tiempo real)
            if (distancia < 10) {
                LedOff(LED_1);
                LedOff(LED_2);
                LedOff(LED_3);
            } else if (distancia >= 10 && distancia < 20) {
                LedOn(LED_1);
                LedOff(LED_2);
                LedOff(LED_3);
            } else if (distancia >= 20 && distancia <= 30) {
                LedOn(LED_1);
                LedOn(LED_2);
                LedOff(LED_3);
            } else if (distancia > 30) {
                LedOn(LED_1);
                LedOn(LED_2);
                LedOn(LED_3);
            }

            // 2. CONTROL DEL DISPLAY BCD Y VARIABLE A MOSTRAR
            if(modo_maximo){
                distancia_lcd = max_distancia;
            }
            else if(!hold){ 
                distancia_lcd = distancia;
            }

            LcdItsE0803Write(distancia_lcd);

            // 3. ENVÍO VÍA PUERTO SERIE
            if (modo_maximo) {
                UartSendString(UART_PC, "MAX: ");
            }

            UartSendString(UART_PC, (char *)UartItoa(distancia_lcd, 10));

            if(unidades_cm){
                UartSendString(UART_PC, " cm\r\n");
            } 
            else {
                UartSendString(UART_PC, " inch\r\n");
            }
        }
        else {
            LcdItsE0803Off();
            LedsOffAll();
        }
    }
}

/**
 * @brief Tarea de FreeRTOS encargada de la medición periódica de distancia con el sensor HC-SR04.
 * 
 * Espera la notificación de la ISR del Timer A. Si @ref on es `true`, realiza la medición
 * en la unidad activa (@ref unidades_cm), actualiza la variable @ref distancia y mantiene
 * el registro de @ref max_distancia. Si está inactivo (`false`), resetea la distancia a `0`.
 * 
 * @param[in] pvParameter Puntero a parámetros pasados a la tarea (no utilizado).
 */
static void MedirTask(void *pvParameter){
    HcSr04Init(GPIO_3, GPIO_2); 

    while(true){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); 
        if(on){
            if(unidades_cm){ 
                distancia = HcSr04ReadDistanceInCentimeters();
            }
            else{
                distancia = HcSr04ReadDistanceInInches();
                UartSendString(UART_PC, " Pulgadas: ");
            }

            if(distancia > max_distancia){
                max_distancia = distancia;
            }
        } 
        else {
            distancia = 0;
        }
    }
}

/**
 * @brief Callback de interrupción asociado a la tecla TEC1 (SWITCH_1).
 * 
 * Alterna el valor de la bandera global @ref on entre encendido (`true`) y apagado (`false`).
 * 
 * @param[in] pvParameter Puntero genérico a parámetros de interrupción (no utilizado).
 */
static void cambio_on(void *pvParameter){
    on = !on;
}

/**
 * @brief Callback de interrupción asociado a la tecla TEC2 (SWITCH_2).
 * 
 * Alterna el valor de la bandera global @ref hold para congelar o descongelar el valor del LCD.
 * 
 * @param[in] pvParameter Puntero genérico a parámetros de interrupción (no utilizado).
 */
static void cambio_hold(void *pvParameter){
    hold = !hold;
}

/*==================[external functions definition]==========================*/

/**
 * @brief Función principal de la aplicación (`main`).
 * 
 * Inicializa los periféricos, configura e inicia la UART_PC a 115200 baudios,
 * configura e inicia los timers de hardware, asocia las interrupciones para las
 * teclas físicas y crea las tareas de FreeRTOS (`MedirTask` y `MostrarTask`).
 */
void app_main(void){
    LedsInit();
    SwitchesInit();

    serial_config_t my_uart = {
        .port      = UART_PC,
        .baud_rate = 115200,
        .func_p    = FuncUART,
        .param_p   = NULL
    };
    UartInit(&my_uart);

    timer_config_t timer_Medir = {
        .timer   = TIMER_A,
        .period  = periodo_medir_us,
        .func_p  = FuncTimerA,
        .param_p = NULL
    };
    TimerInit(&timer_Medir);
    
    timer_config_t timer_Mostrar = {
        .timer   = TIMER_B,
        .period  = CONFIG_BLINK_PERIOD_Mostrar_US,
        .func_p  = FuncTimerB,
        .param_p = NULL
    };
    TimerInit(&timer_Mostrar);

    HcSr04Init(GPIO_3, GPIO_2); 
    LcdItsE0803Init();

    xTaskCreate(&MedirTask, "Medir", 2048, NULL, 5, &Medir_task_handle);
    xTaskCreate(&MostrarTask, "Mostrar", 2048, NULL, 5, &Mostrar_task_handle);
    
    TimerStart(timer_Medir.timer);
    TimerStart(timer_Mostrar.timer);

    SwitchActivInt(SWITCH_1, cambio_on, NULL);
    SwitchActivInt(SWITCH_2, cambio_hold, NULL);
}