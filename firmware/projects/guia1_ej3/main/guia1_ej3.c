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

/*==================[macros and definitions]=================================*/
#define ON     1
#define OFF    0
#define TOGGLE 2

typedef struct
{
    uint8_t mode;      /* ON, OFF, TOGGLE */
    uint8_t n_led;     /* Indica el número de LED a controlar */
    uint8_t n_ciclos;  /* Indica la cantidad de ciclos de encendido/apagado */
    uint16_t periodo;  /* Indica el tiempo de cada ciclo */
} my_leds;

/*==================[internal functions declaration]=========================*/
void ControlLeds(my_leds *leds);

/*==================[external functions definition]==========================*/
void app_main(void)
{
    LedsInit();

    // 1. Instanciamos la estructura
    my_leds mi_led;

    // 2. Cargamos parámetros de prueba
    mi_led.mode = TOGGLE;
    mi_led.n_led = 1;
    mi_led.n_ciclos = 5;
    mi_led.periodo = 500;

    ControlLeds(&mi_led); // Paso del puntero a la función
}

/*==================[internal functions definition]==========================*/
void ControlLeds(my_leds *leds)
{
    // Evaluamos el modo configurado en la estructura
    switch (leds->mode)
    {
    case ON:
        switch (leds->n_led)
        {
        case 1:
            LedOn(LED_1);
            break;
        case 2:
            LedOn(LED_2);
            break;
        case 3:
            LedOn(LED_3);
            break;
        }
        break;

    case OFF:
        switch (leds->n_led)
        {
        case 1:
            LedOff(LED_1);
            break;
        case 2:
            LedOff(LED_2);
            break;
        case 3:
            LedOff(LED_3);
            break;
        }
        break;

    case TOGGLE:
    {
        uint8_t i = 0;

        // Bucle principal para contar la cantidad de ciclos
        while (i < (leds->n_ciclos * 2))   /*n_ciclos * 2 para tener en cuenta ambos estados (encendido y apagado)*/
        {
            switch (leds->n_led)
            {
            case 1:
                LedToggle(LED_1);
                break;
            case 2:
                LedToggle(LED_2);
                break;
            case 3:
                LedToggle(LED_3);
                break;
            }

            i++;

            // Bucle de retardo
            uint16_t retardo = leds->periodo / 100;

            for (uint16_t j = 0; j < retardo; j++)
            {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        break;
    }

    default:
        break;
    }
}
/*==================[end of file]============================================*/