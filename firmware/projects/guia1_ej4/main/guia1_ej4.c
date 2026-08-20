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
/*==================[macros and definitions]=================================*/

/*==================[internal data definition]===============================*/

/*==================[internal functions declaration]=========================*/

int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number);

/*==================[external functions definition]==========================*/
void app_main(void)
{
    uint32_t numero = 209;
    uint8_t digitos = 4;
    uint8_t arreglo_bcd[4];

    // Llamada a la función
    convertToBcdArray(numero, digitos, arreglo_bcd);

	// Imprimir el contenido del arreglo/puntero por consola
    printf("Numero original: %ld\n", numero);
    printf("Contenido del arreglo BCD:\n");
    for (uint8_t i = 0; i < digitos; i++)
    {
        printf("arreglo_bcd[%d] = %d\n", i, arreglo_bcd[i]);
    }
}
/*==================[internal functions definition]==========================*/
int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number)
{
// Bucle para i desde 0 hasta cantidadDigitos-1

    for (uint8_t i = 0; i < digits; i++)
    {
        uint8_t digito = data % 10;    // digito = numero MOD 10
        bcd_number[i]  = digito;       // puntero[i] = digito
        data           = data / 10;    // numero = numero DIV 10
    }

    return 0;
}

/*==================[end of file]============================================*/