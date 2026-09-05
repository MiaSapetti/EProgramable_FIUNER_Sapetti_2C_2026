/*! @mainpage Medidor de Distancia por Ultrasonido
 *
 * \section genDesc General Description
 *
 * Medición de distancia utilizando un sensor ultrasónico HC-SR04, visualización 
 * de la lectura en un display LCD y representación del rango mediante LEDs, 
 * gestionado a través de tareas independientes en FreeRTOS.
 * 
 * \section diagram Diagramas de Flujo
 * @image html Diagrama_de_Flujo_Tarea_Medir.png 
 * @image html Diagrama_de_Flujo_Tarea_Mostrar.png 
 * @image html Diagrama_de_Flujo_Tarea_Teclas.png 
 * 
 * @section changelog Changelog
 *
 * |    Date    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 04/09/2026 | Document creation                              |
 *
 * @author Mia Sapetti (mia.sapetti@ingenieria.uner.edu.ar)
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

/*==================[macros and definitions]=================================*/
/**
 * @brief Tiempo de refresco para las tareas de medición y visualización en milisegundos.
 */
#define REFRESCO_MEDICION_MS 1000

/**
 * @brief Tiempo de refresco para el monitoreo de teclas en milisegundos.
 */
#define REFRESCO_TECLAS_MS   500

/*==================[internal data definition]===============================*/
/**
 * @brief Handle de la tarea encargada de la visualización en LEDs y LCD.
 */
TaskHandle_t Mostrar_task_handle = NULL;

/**
 * @brief Handle de la tarea encargada de la medición de distancia.
 */
TaskHandle_t Medir_task_handle   = NULL;

/**
 * @brief Handle de la tarea encargada de la lectura de las teclas.
 */
TaskHandle_t Teclas_task_handle  = NULL;

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
 * @brief Tarea de FreeRTOS encargada de controlar el estado de los LEDs y actualizar el display LCD.
 * 
 * Evalúa la variable global @ref distancia para encender los LEDs correspondientes según los rangos:
 *  - Distancia < 10 cm: Todos los LEDs apagados.
 *  - 10 cm <= Distancia < 20 cm: Enciende LED 1.
 *  - 20 cm <= Distancia <= 30 cm: Encienden LED 1 y LED 2.
 *  - Distancia > 30 cm: Encienden LED 1, LED 2 y LED 3.
 * 
 * Además, actualiza la lectura del LCD si la variable @ref hold está desactivada. Si @ref on es `false`,
 * apaga tanto el LCD como los LEDs.
 * 
 * @param[in] pvParameter Puntero a parámetros pasados a la tarea (no utilizado).
 */
static void MostrarTask(void *pvParameter){
    // Inicialización del LCD dentro de la tarea
    LcdItsE0803Init();

    while(true){
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

        vTaskDelay(REFRESCO_MEDICION_MS / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Tarea de FreeRTOS encargada de la medición periódica de distancia con el sensor ultrasónico HC-SR04.
 * 
 * Si el flag global @ref on está activo (`true`), realiza la lectura de distancia en centímetros y actualiza
 * la variable global @ref distancia. Si está inactivo (`false`), resetea el valor a `0`.
 * 
 * @param[in] pvParameter Puntero a parámetros pasados a la tarea (no utilizado).
 */
static void MedirTask(void *pvParameter){
    // Inicialización del sensor HC-SR04 dentro de la tarea
    HcSr04Init(GPIO_3, GPIO_2); // Reemplaza por los GPIOs de tu placa

    while(true){
        if(on){
            distancia = HcSr04ReadDistanceInCentimeters();
        } 
		else {
            distancia = 0;
        }

        vTaskDelay(REFRESCO_MEDICION_MS / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Tarea de FreeRTOS encargada de leer el estado de las teclas/switches y alternar las variables de control.
 * 
 *  - **TEC1 (`SWITCH_1`)**: Alterna el estado de la variable global @ref on (ON / OFF del sistema).
 *  - **TEC2 (`SWITCH_2`)**: Alterna el estado de la variable global @ref hold (HOLD de lectura en LCD).
 * 
 * @param[in] pvParameter Puntero a parámetros pasados a la tarea (no utilizado).
 */
static void TeclasTask(void *pvParameter){
    SwitchesInit();
    uint8_t teclas;

    while(true){
        teclas = SwitchesRead();

        // TEC1: Alterna ON / OFF
        if(teclas & SWITCH_1){
            on = !on;
        }

        // TEC2: Alterna HOLD
        if(teclas & SWITCH_2){
            hold = !hold;
        }

        vTaskDelay(REFRESCO_TECLAS_MS / portTICK_PERIOD_MS);
    }
}

/*==================[external functions definition]==========================*/
/**
 * @brief Función principal de la aplicación (`main`).
 * 
 * Inicializa los periféricos de la placa (LEDs, Switches, Sensor HC-SR04 y LCD) y
 * crea las tareas necesarias de FreeRTOS (`MedirTask`, `MostrarTask` y `TeclasTask`).
 */
void app_main(void){
    // Inicialización de periféricos de la placa
    LedsInit();
    SwitchesInit();

    // Inicialización del sensor y display
    HcSr04Init(GPIO_3, GPIO_2); // Modificar pines según correspondan
    LcdItsE0803Init();

    // Creación de tareas
    xTaskCreate(&MedirTask, "Medir", 2048, NULL, 5, &Medir_task_handle);
    xTaskCreate(&MostrarTask, "Mostrar", 2048, NULL, 5, &Mostrar_task_handle);
    xTaskCreate(&TeclasTask, "Teclas", 1024, NULL, 5, &Teclas_task_handle);
}
