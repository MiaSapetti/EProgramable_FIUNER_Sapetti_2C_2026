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
#include "gpio_mcu.h"
/*==================[macros and definitions]=================================*/
typedef struct
{
    gpio_t pin; /*!< GPIO pin number */
    io_t dir;   /*!< GPIO direction '0' IN; '1' OUT*/
} gpioConf_t;
/*==================[internal data definition]===============================*/

/*==================[internal functions declaration]=========================*/
void setBCDtoGPIO(uint8_t bcd_digit, gpioConf_t *gpio_vector);
/*==================[external functions definition]==========================*/
void app_main(void)
{
    // 1. Definición del vector mapeando bits b0..b3 a los GPIOs indicados
    gpioConf_t gpio_bcd[4] = {
        {GPIO_20, GPIO_OUTPUT}, // b0 -> GPIO_20
        {GPIO_21, GPIO_OUTPUT}, // b1 -> GPIO_21
        {GPIO_22, GPIO_OUTPUT}, // b2 -> GPIO_22
        {GPIO_23, GPIO_OUTPUT}  // b3 -> GPIO_23
    };

    // 2. Inicialización y configuración previa de cada GPIO
    for (uint8_t i = 0; i < 4; i++)
    {
        GPIOInit(gpio_bcd[i].pin, gpio_bcd[i].dir);
    }

    // 3. Prueba de la función enviando un dígito BCD (Ejemplo: 6 -> '0110')
    uint8_t digito_bcd = 6;
    setBCDtoGPIO(digito_bcd, gpio_bcd);
}

/*==================[internal functions definition]==========================*/

void setBCDtoGPIO(uint8_t bcd_digit, gpioConf_t *gpio_vector)
{
    for (uint8_t b = 0; b < 4; b++)
    {
        // Se extrae el estado del bit 'b' usando desplazamiento, lo llevo a la posicion cero y enmáscara el bit menos significativo
        if ((bcd_digit >> b) & 0x01)
        {
            GPIOOn(gpio_vector[b].pin);  // Poner en ALTO ('1')
        }
        else
        {
            GPIOOff(gpio_vector[b].pin); // Poner en BAJO ('0')
        }
    }
}
/*==================[end of file]============================================*/