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
int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number);
void setBCDtoGPIO(uint8_t bcd_digit, gpioConf_t *gpio_vector);
void showDisplay(uint32_t data, uint8_t digits, gpioConf_t *gpio_bcd, gpioConf_t *gpio_select);
/*==================[external functions definition]==========================*/
void app_main(void)
{
	// 1. Mapeo de pines para el bus BCD (b0 a b3) - Ejercicio 5
    gpioConf_t gpio_bcd[4] = {
        {GPIO_20, GPIO_OUTPUT}, // b0
        {GPIO_21, GPIO_OUTPUT}, // b1
        {GPIO_22, GPIO_OUTPUT}, // b2
        {GPIO_23, GPIO_OUTPUT}  // b3
    };

    // 2. Mapeo de pines de selección de dígitos para multiplexado - Ejercicio 6
    gpioConf_t gpio_select[3] = {
        {GPIO_19, GPIO_OUTPUT}, // Selector Dígito 1
        {GPIO_18, GPIO_OUTPUT}, // Selector Dígito 2
        {GPIO_9,  GPIO_OUTPUT}  // Selector Dígito 3
    };

	// 3. Inicialización de todos los pines
    for (uint8_t i = 0; i < 4; i++)
    {
        GPIOInit(gpio_bcd[i].pin, gpio_bcd[i].dir);
    }
    for (uint8_t i = 0; i < 3; i++)
    {
        GPIOInit(gpio_select[i].pin, gpio_select[i].dir);
    }

	// 4. Muestra del número en el Display
    uint32_t numero = 209;
    showDisplay(numero, 3, gpio_bcd, gpio_select);
}

/*==================[internal functions definition]==========================*/

/**
 * @brief Descompone un entero en un arreglo BCD (Ejercicio 4)
 */
int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number)
{
// Bucle para i desde 0 hasta cantidadDigitos-1
   for (int8_t i = digits - 1; i >= 0; i--)   //guarda datos en orden coorrecto
    {
     uint8_t digito = data % 10;    // digito = numero MOD 10
        bcd_number[i]  = digito;       // puntero[i] = digito
        data           = data / 10;    // numero = numero DIV 10
    }
    return 0;
}

/**
 * @brief Escribe un dígito BCD en los GPIOs del bus de datos (Ejercicio 5)
 */
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

/**
 * @brief Recibe el número, lo convierte a BCD y multiplexa el envío a los displays (Ejercicio 6)
 */
void showDisplay(uint32_t data, uint8_t digits, gpioConf_t *gpio_bcd, gpioConf_t *gpio_select)
{
    uint8_t bcd_array[10]; // Arreglo auxiliar para guardar los dígitos extraídos

    // Paso 1: Convertir el número a arreglo BCD
    convertToBcdArray(data, digits, bcd_array);

    // Paso 2: Recorrer cada dígito, cargarlo en el bus BCD y multiplexar
    for (uint8_t i = 0; i < digits; i++)
    {
        // Pone en el puerto BCD el dígito actual
        setBCDtoGPIO(bcd_array[i], gpio_bcd);
        printf("bcd_array[%d] = %d\n", i, bcd_array[i]);

        // Genera el pulso de habilitación/latcheo en el dígito correspondiente
        GPIOOn(gpio_select[i].pin);
        GPIOOff(gpio_select[i].pin);
    }
}
/*==================[end of file]============================================*/