/*! @mainpage Control de Display BCD de 7 Segmentos Multiplexado
 *
 * @section genDesc Descripción General
 *
 * Este programa permite descomponer un número entero en sus dígitos BCD (Binary-Coded Decimal) 
 * y mostrarlos dinámicamente en un display de 7 segmentos de 3 dígitos mediante un bus de datos 
 * de 4 bits y un bus de selección de dígitos (multiplexado).
 *
 * @section hardConn Conexión de Hardware
 *
 * |   Componente Bus / Función   |   ESP32 (GPIO)   |
 * |:----------------------------:|:-----------------|
 * | Bus BCD Bit 0 (b0)           | GPIO_20          |
 * | Bus BCD Bit 1 (b1)           | GPIO_21          |
 * | Bus BCD Bit 2 (b2)           | GPIO_22          |
 * | Bus BCD Bit 3 (b3)           | GPIO_23          |
 * | Selector Dígito 1            | GPIO_19          |
 * | Selector Dígito 2            | GPIO_18          |
 * | Selector Dígito 3            | GPIO_9           |
 *
 * @section changelog Changelog
 *
 * |    Fecha   | Descripción                                    |
 * |:----------:|:-----------------------------------------------|
 * | 05/09/2026 | Documentación completa del código con Doxygen  |
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
#include "gpio_mcu.h"

/*==================[macros and definitions]=================================*/
/**
 * @struct gpioConf_t
 * @brief Estructura de configuración de pines GPIO.
 * 
 * Permite agrupar el número de pin GPIO y la dirección de trabajo (Entrada/Salida).
 */
typedef struct
{
    gpio_t pin; /*!< Número de pin GPIO asignado */
    io_t dir;   /*!< Dirección del GPIO: '0' para Entrada (IN), '1' para Salida (OUT) */
} gpioConf_t;

/*==================[internal data definition]===============================*/

/*==================[internal functions declaration]=========================*/

/**
 * @brief Descompone un número entero positivo en un arreglo de dígitos en formato BCD.
 * 
 * @param[in] data Número entero a convertir.
 * @param[in] digits Cantidad de dígitos que componen el número a representar.
 * @param[out] bcd_number Puntero al arreglo donde se almacenarán los dígitos descompuestos.
 * @return int8_t Devuelve `0` al finalizar correctamente la conversión.
 */
int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number);

/**
 * @brief Establece el valor de un dígito BCD en los pines GPIO asignados al bus de datos de 4 bits.
 * 
 * @param[in] bcd_digit Dígito BCD (0 a 9) a colocar en el bus de salida.
 * @param[in] gpio_vector Puntero al vector de estructuras @ref gpioConf_t con los 4 pines del bus BCD.
 */
void setBCDtoGPIO(uint8_t bcd_digit, gpioConf_t *gpio_vector);

/**
 * @brief Controla la secuencia completa para mostrar un número entero en el display de 7 segmentos.
 * 
 * Convierte el número recibido a un arreglo BCD y posteriormente multiplexa el envío
 * de cada dígito activando y desactivando la línea de selección correspondiente.
 * 
 * @param[in] data Número entero a mostrar en pantalla.
 * @param[in] digits Cantidad de dígitos a multiplexar.
 * @param[in] gpio_bcd Vector con la configuración de los pines del bus BCD de datos.
 * @param[in] gpio_select Vector con la configuración de los pines de selección de cada dígito.
 */
void showDisplay(uint32_t data, uint8_t digits, gpioConf_t *gpio_bcd, gpioConf_t *gpio_select);

/*==================[external functions definition]==========================*/

/**
 * @brief Función principal del programa (`main`).
 * 
 * Configura los pines GPIO del bus BCD y del multiplexor, inicializa el hardware y 
 * envía un valor de prueba (`209`) para mostrar en el display de 7 segmentos.
 */
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
 * @brief Descompone un entero en un arreglo BCD (Ejercicio 4).
 */
int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number)
{
    // Bucle para i desde cantidadDigitos-1 hasta 0
    for (int8_t i = digits - 1; i >= 0; i--)   // Guarda datos en el orden correcto
    {
        uint8_t digito = data % 10;    // digito = numero MOD 10
        bcd_number[i]  = digito;       // puntero[i] = digito
        data           = data / 10;    // numero = numero DIV 10
    }
    return 0;
}

/**
 * @brief Escribe un dígito BCD en los GPIOs del bus de datos (Ejercicio 5).
 */
void setBCDtoGPIO(uint8_t bcd_digit, gpioConf_t *gpio_vector)
{
    for (uint8_t b = 0; b < 4; b++)
    {
        // Se extrae el estado del bit 'b' usando desplazamiento y enmascarando el bit menos significativo
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
 * @brief Recibe el número, lo convierte a BCD y multiplexa el envío a los displays (Ejercicio 6).
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