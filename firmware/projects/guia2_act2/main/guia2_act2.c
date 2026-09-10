/**
 * @file guia2_act2.c
 * @mainpage Medidor de Distancia por Ultrasonido con Interrupciones
 *
 * @section genDesc Descripción General
 *
 * Medición de distancia utilizando un sensor ultrasónico HC-SR04, visualización 
 * de la lectura en un display LCD BCD y representación del rango mediante LEDs, 
 * gestionado a través de timers e interrupciones de teclas en FreeRTOS.
 * 
 * 
 * @section changelog Historial de Cambios
 *
 * |    Fecha   | Descripción                                           |
 * |:----------:|:------------------------------------------------------|
 * | 10/09/2026 | Integración de timers e interrupciones de teclas      |
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

/*==================[macros and definitions]=================================*/

/**
 * @def CONFIG_BLINK_PERIOD_Medir_US
 * @brief Período de disparo del Timer A para la tarea de medición (en microsegundos).
 */
#define CONFIG_BLINK_PERIOD_Medir_US 1000000 

/**
 * @def CONFIG_BLINK_PERIOD_Mostrar_US
 * @brief Período de disparo del Timer B para la tarea de visualización (en microsegundos).
 */
#define CONFIG_BLINK_PERIOD_Mostrar_US 1000000

/*==================[internal data definition]===============================*/

/**
 * @brief Handle de la tarea encargada de la medición de distancia.
 */
TaskHandle_t Medir_task_handle   = NULL;

/**
 * @brief Handle de la tarea encargada de la visualización en LEDs y display LCD.
 */
TaskHandle_t Mostrar_task_handle   = NULL;

/**
 * @brief Variable global que almacena el valor actual de la distancia medida en centímetros.
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
 * @brief Tarea de FreeRTOS encargada del control de LEDs y la actualización del display BCD.
 * 
 * Permanece bloqueada esperando la notificación enviada por la ISR del Timer B.
 * Si @ref on es `true`, enciende los LEDs según la distancia medida y actualiza
 * el display BCD (respetando la condición de congelamiento de @ref hold). 
 * Si @ref on es `false`, apaga el display y todos los LEDs.
 * 
 * @param[in] pvParameter Puntero a parámetros de tarea (no utilizado).
 */
static void MostrarTask(void *pvParameter){
    // Inicialización del LCD dentro de la tarea
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

            // 2. CONTROL DEL DISPLAY BCD (Retiene valor si 'hold' es true)
            if(!hold){
                distancia_lcd = distancia;
            }

            // Muestra directamente el entero (0 a 999) en el display
            LcdItsE0803Write(distancia_lcd);

        }
        else {
            // Si 'on' es false, apaga el display y los LEDs
            LcdItsE0803Off();
            LedsOffAll();
        }
    }
}

/**
 * @brief Tarea de FreeRTOS encargada de la medición periódica de distancia con el sensor ultrasónico HC-SR04.
 * 
 * Permanece bloqueada esperando la notificación enviada por la ISR del Timer A.
 * Si el flag global @ref on está activo (`true`), realiza la lectura de distancia en centímetros y actualiza
 * la variable global @ref distancia. Si está inactivo (`false`), resetea el valor a `0`.
 * 
 * @param[in] pvParameter Puntero a parámetros pasados a la tarea (no utilizado).
 */
static void MedirTask(void *pvParameter){
    // Inicialización del sensor HC-SR04 dentro de la tarea
    HcSr04Init(GPIO_3, GPIO_2); 

    while(true){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); 
        if(on){
            distancia = HcSr04ReadDistanceInCentimeters();
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
 * Inicializa los periféricos de la placa, configura e inicia los timers de hardware,
 * establece los handlers de interrupción para los switches y crea las tareas FreeRTOS
 * (`MedirTask` y `MostrarTask`).
 */
void app_main(void){
    // Inicialización de periféricos de la placa
    LedsInit();
    SwitchesInit();

    /* Inicialización de timers */
    timer_config_t timer_Medir = {
        .timer   = TIMER_A,
        .period  = CONFIG_BLINK_PERIOD_Medir_US,
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

    // Inicialización del sensor y display
    HcSr04Init(GPIO_3, GPIO_2); 
    LcdItsE0803Init();

    // Creación de tareas
    xTaskCreate(&MedirTask, "Medir", 2048, NULL, 5, &Medir_task_handle);
    xTaskCreate(&MostrarTask, "Mostrar", 2048, NULL, 5, &Mostrar_task_handle);
    
    /* Inicialización del conteo de timers */
    TimerStart(timer_Medir.timer);
    TimerStart(timer_Mostrar.timer);

    // Configuración de Interrupciones para las Teclas
    SwitchActivInt(SWITCH_1, cambio_on, NULL);
    SwitchActivInt(SWITCH_2, cambio_hold, NULL);
}