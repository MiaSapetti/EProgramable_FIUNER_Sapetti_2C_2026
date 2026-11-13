/*! @mainpage Evaluador de Temblor Neuromuscular y Apraxia
 *
 * @section genDesc Descripción General
 *
 * Este programa implementa un sistema embebido en ESP32 para la adquisición, filtrado digital, 
 * procesamiento espectral (FFT) y cuantificación de parámetros de temblor neuromuscular y apraxia.
 * 
 * El sistema permite ejecutar pruebas estáticas y dinámicas seleccionadas mediante pulsadores,
 * adquiriendo aceleración triaxial desde un módulo ADXL335 a una frecuencia de muestreo de 100 Hz.
 * Realiza un filtrado pasa altas (1 Hz) para remover la componente continua/gravedad y deriva lenta, 
 * y un filtrado pasa bajas (15 Hz) para atenuar ruido de alta frecuencia.
 * 
 * Los datos espectrales y las métricas de severidad (RMS y Frecuencia Dominante) se transmiten 
 * en tiempo real tanto por UART (PC) como por Bluetooth SPP (BLE) hacia una interfaz gráfica.
 *
 * @section hardConn Conexión de Hardware
 *
 * | Peripheral     | ESP32 GPIO |
 * |:--------------:|:-----------|
 * | ADXL335 (X)    | CH0 (ADC)  |
 * | ADXL335 (Y)    | CH1 (ADC)  |
 * | ADXL335 (Z)    | CH2 (ADC)  |
 * | Switch 1       | GPIO_4     |
 * | Switch 2       | GPIO_15    |
 * | Buzzer         | GPIO_20    |
 *
 *
 * |   Fecha    | Descripción                                                |
 * |:----------:|:-----------------------------------------------------------|
 * | 12/09/2023 | Creación inicial del documento                             |
 * | 29/10/2026 | Integración de BLE, promediado de ventanas y métricas RMS  |
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
#include <string.h>
#include <math.h>
#include "led.h"
#include "switch.h"
#include "buzzer.h"
#include "ble_mcu.h"
#include "uart_mcu.h"
#include "delay_mcu.h"
#include "timer_mcu.h"
#include "esp_log.h"
#include "ADXL335.h"
#include "fft.h"
#include "iir_filter.h"
/*==================[macros and definitions]=================================*/
/** @brief Período de parpadeo de configuración por defecto (ms). */
#define CONFIG_BLINK_PERIOD                 500

/** @brief Cantidad de muestras del buffer para el procesamiento FFT (Potencia de 2). */
#define BUFFER_SIZE                         128

/** @brief Frecuencia de muestreo de la señal de aceleración (Hz). */
#define SAMPLE_FREC                         100

/** @brief Período de muestreo en microsegundos (10000 us = 10 ms -> 100 Hz). */
#define CONFIG_BLINK_PERIOD_Adquisicion_US  10000

/** @brief Valor constante de aceleración de la gravedad ($m/s^2$). */
#define GRAVEDAD_MS2                        9.80665f

/** @brief Duración total de la prueba estática en milisegundos (10 segundos). */
#define TIEMPO_PRUEBA_ESTATICA_MS           10000

/** @brief Bin máximo de envío FFT (bin 32 correspondiente a 25.0 Hz). */
#define BIN_ENVIO_MAX                       32

/** @brief Pausa entre transmisiones de cadenas de texto por UART y BLE (ms). */
#define PAUSA_ENVIO_MS                      25

/** @brief Umbral RMS mínimo de aceleración para clasificar la presencia de temblor ($m/s^2$). */
#define UMBRAL_RMS_TEMBLOR                  0.30f

/** @brief Identificador del módulo para logs de ESP-IDF. */
static const char *TAG = "EVALUADOR_TEMBLOR";

/**
 * @brief Enumeración de los estados posibles en la secuencia de evaluación.
 */
typedef enum {
    MODO_REPOSO,   /**< Sistema en espera de inicio de prueba */
    MODO_ESTATICO, /**< Prueba de temblor postural/estático activa */
    MODO_DINAMICO  /**< Prueba de temblor cinético/dinámico activa */
} estado_prueba_t;
/*==================[internal data definition]===============================*/
/** @brief Handle para la tarea FreeRTOS de adquisición de datos. */
TaskHandle_t Adquisicion_task_handle = NULL;

/** @brief Handle para la tarea FreeRTOS de procesamiento digital de señales. */
TaskHandle_t Procesamiento_task_handle = NULL;

/** @brief Handle para la tarea FreeRTOS de gestión de temporización y avisos de pruebas. */
TaskHandle_t Pruebas_task_handle = NULL;

/** @brief Módulo instantáneo de la aceleración triaxial ($m/s^2$). */
float modulo_ms2 = 0;

/** @brief Buffer circular donde se acumulan las muestras de aceleración adquiridas. */
static float buffer_aceleracion[BUFFER_SIZE];

/** @brief Buffer de trabajo desacoplado para la ejecución del procesamiento FFT y filtrado. */
static float buffer_procesamiento[BUFFER_SIZE];

/** @brief Índice del buffer de adquisición. */
static volatile uint16_t buffer_index = 0;

// Arreglos de procesamiento para el módulo FFT
/** @brief Buffer intermedio para la salida del filtrado pasa altas. */
static float buffer_filt[BUFFER_SIZE];

/** @brief Buffer final para la salida del filtrado pasa bajas. */
static float buffer_filt2[BUFFER_SIZE];

/** @brief Vector que almacena el espectro de magnitud filtrado de la FFT. */
static float espectro_filt_fft[BUFFER_SIZE / 2];

/** @brief Vector que almacena la escala de frecuencias calculada para los bins de la FFT. */
static float f[BUFFER_SIZE / 2];

//Variable Globales que almacenan resultados de procesamiento
/** @brief Valor RMS de aceleración filtrada en el bloque actual ($m/s^2$). */
float rms_temblor = 0.0f;

/** @brief Frecuencia de mayor magnitud detectada en el bloque actual (Hz). */
float frecuencia_dominante = 0.0f;

// Control de estados
/** @brief Estado actual del flujo de pruebas. */
volatile estado_prueba_t estado_actual = MODO_REPOSO;

//Variables totales para aplicación BLE
/** @brief Espectro acumulado para promediar la respuesta frecuencial de toda la prueba. */
static float espectro_acum[BUFFER_SIZE / 2];

/** @brief Acumulador del valor cuadrático medio de RMS a lo largo de las ventanas. */
static float rms2_acum = 0.0f;

/** @brief Contador de ventanas completas válidas procesadas en una prueba. */
static int n_ventanas = 0;

/** @brief Bandera para descartar el primer bloque de datos debido al transitorio de los filtros. */
static volatile bool primera_ventana = true;
/*==================[internal functions declaration]=========================*/
/**
 * @brief Callback de temporizador ejecutado por la interrupción de Timer A.
 * @param param Puntero genérico a parámetros de la función (no utilizado).
 */
void FuncTimerA(void* param){
    vTaskNotifyGiveFromISR(Adquisicion_task_handle, pdFALSE);
}

/**
 * @brief Interrupción por hardware al presionar la Tecla 1 (Selecciona e Inicia Prueba Estática).
 */
void Tecla1_ISR(void){
    // Selecciona e Inicia Prueba Estática
    buffer_index = 0; // primero limpia para que cada prueba empieza con el buffer vacío y arrastre valores viejos
    estado_actual = MODO_ESTATICO;
    if(Pruebas_task_handle != NULL){
        vTaskNotifyGiveFromISR(Pruebas_task_handle, pdFALSE);
    }
}

/**
 * @brief Interrupción por hardware al presionar la Tecla 2 (Selecciona e Inicia Prueba Dinámica).
 */
void Tecla2_ISR(void){
    // Selecciona e Inicia Prueba Dinámica
    buffer_index = 0;   // primero limpia para que cada prueba empieza con el buffer vacío y arrastre valores viejos
    estado_actual = MODO_DINAMICO;
    if(Pruebas_task_handle != NULL){
        vTaskNotifyGiveFromISR(Pruebas_task_handle, pdFALSE);
    }
}

/**
 * @brief Envía una cadena de texto formateada por Bluetooth SPP.
 * @param s Puntero a la cadena de caracteres terminada en nulo a transmitir.
 */
static void EnviarLinea(const char *s)
{
    BleSendString((char *)s);
    vTaskDelay(PAUSA_ENVIO_MS / portTICK_PERIOD_MS);
}
/**
 * @brief Reinicia las variables acumuladoras y contadores al iniciar una nueva prueba.
 */
static void ReiniciarAcumuladores(void)
{
    memset(espectro_acum, 0, sizeof(espectro_acum));
    rms2_acum = 0.0f;
    n_ventanas = 0;
    primera_ventana = true;
}
/**
 * @brief Calcula y envía los resultados consolidados (RMS promedio, espectro medio y frecuencia dominante) al finalizar una prueba.
 */
static void EnviarResultadoFinal(void)
{
    // Deja terminar de enviar la última ventana de ProcesamientoTask
    vTaskDelay(1500 / portTICK_PERIOD_MS);

    if (n_ventanas == 0) {
        ESP_LOGW(TAG, "Sin ventanas validas para el resultado final");
        return;
    }

    float rms_total = sqrtf(rms2_acum / (float)n_ventanas);

    // Espectro promedio y su pico
    float espectro_prom[BIN_ENVIO_MAX + 1];
    int   idx_pico = 1;
    float max_prom = 0.0f;
    for (int i = 1; i <= BIN_ENVIO_MAX; i++) {
        espectro_prom[i] = espectro_acum[i] / (float)n_ventanas;
        if (espectro_prom[i] > max_prom) {
            max_prom = espectro_prom[i];
            idx_pico = i;
        }
    }
    float f_total = f[idx_pico];

    // Envío: espectro promedio y métricas finales
    char msg[32];
    EnviarLinea("*GC*");
    for (int i = 1; i <= BIN_ENVIO_MAX; i++) {
        snprintf(msg, sizeof(msg), "*GX%.1fY%.2f*", f[i], espectro_prom[i]);
        EnviarLinea(msg);
    }
    if (rms_total > UMBRAL_RMS_TEMBLOR) {
        snprintf(msg, sizeof(msg), "*D%.2f*", f_total);
    } 
    else {
        snprintf(msg, sizeof(msg), "*DSin temblor*");
    }
    EnviarLinea(msg);
    snprintf(msg, sizeof(msg), "*S%.2f*", rms_total);
    EnviarLinea(msg);

    ESP_LOGI(TAG, "RESULTADO: RMS=%.2f m/s^2 | Frec=%.2f Hz | %d ventanas", rms_total, f_total, n_ventanas);
}

/**
 * @brief Tarea FreeRTOS encargada de muestrear el acelerómetro ADXL335 a 100 Hz y calcular el módulo vectorial de la aceleración.
 * @param pvParameter Puntero a parámetros de tarea de FreeRTOS (no utilizado).
 */
static void AdquisicionTask(void *pvParameter){
    float ax_g, ay_g, az_g;
	while(true){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // Solo adquiere cuando hay una prueba activa
        if(estado_actual != MODO_REPOSO){
            // 1. Obtener mediciones de los 3 ejes
            ax_g = ReadXValue();
            ay_g = ReadYValue();
            az_g = ReadZValue();

            // 2. Convertir d g a m/s²
            float ax_ms2 = ax_g * GRAVEDAD_MS2;
            float ay_ms2 = ay_g * GRAVEDAD_MS2;
            float az_ms2 = az_g * GRAVEDAD_MS2;
        
            // 3. Calcular el módulo vectorial
            modulo_ms2 = sqrtf((ax_ms2 * ax_ms2) + (ay_ms2 * ay_ms2) + (az_ms2 * az_ms2));
            
            // 4. Guardar en el buffer
            //buffer_aceleracion[buffer_index] = az_ms2;  // cambio de modulo_ms2 (pruebas)
            buffer_aceleracion[buffer_index] = modulo_ms2;
            buffer_index++;

            // 5. Verificar si el buffer de muestras está lleno para ser procesado
            if (buffer_index >= BUFFER_SIZE) {
                buffer_index = 0; // Reiniciar índice

                // 6. Cargar datos en el buffer de procesamiento con una copia desacoplada de seguridad
                memcpy(buffer_procesamiento, buffer_aceleracion, sizeof(buffer_aceleracion));
                
                // 7. Avisar a la Tarea de Procesamiento que hay un nuevo bloque listo
                if (Procesamiento_task_handle != NULL) {
                    xTaskNotifyGive(Procesamiento_task_handle);
                }
            }    
        }    
	}
}

/**
 * @brief Tarea FreeRTOS que realiza la remoción de continua, filtrado pasa altas/bajas IIR, FFT y transmisión de tramas de gráficos.
 * @param pvParameter Puntero a parámetros de tarea de FreeRTOS (no utilizado).
 */
static void ProcesamientoTask(void *pvParameter){
	float fft_input[BUFFER_SIZE];
	while(true){
        // Espera bloqueada hasta que la Tarea de Adquisición complete las 128 muestras
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		// 1. Calcular el valor medio (componente continua DC / Gravedad)
        float suma = 0.0f;
        for (int i = 0; i < BUFFER_SIZE; i++) {
            suma += buffer_procesamiento[i];
        }
        float media_dc = suma / (float)BUFFER_SIZE;

        // 2. Remoción de DC (centrar señal en 0) y cálculo de RMS
        for (int i = 0; i < BUFFER_SIZE; i++) {
            fft_input[i] = buffer_procesamiento[i] - media_dc;
        }

        // 3. Filtrado IIR utilizando las funciones de la cátedra
        HiPassFilter(fft_input, buffer_filt, BUFFER_SIZE);
        LowPassFilter(buffer_filt, buffer_filt2, BUFFER_SIZE);
        
        // RMS de la señal sin DC (sin filtrar): debe dar ~9 m/s² para todos los casos de igual amplitud
        //float suma_cuad_raw = 0.0f;
        //for (int i = 0; i < BUFFER_SIZE; i++) {
           // float x = buffer_procesamiento[i] - media_dc;
           // suma_cuad_raw += x * x;
        //}
        //float rms_raw = sqrtf(suma_cuad_raw / (float)BUFFER_SIZE);

        // 4. Cálculo de RMS (Métrica de severidad sobre el buffer filtrado)
        float suma_cuadrados = 0.0f;
        for (int i = 0; i < BUFFER_SIZE; i++) {
            suma_cuadrados += (buffer_filt2[i] * buffer_filt2[i]);
        }
        rms_temblor = sqrtf(suma_cuadrados / (float)BUFFER_SIZE);

       // 5. Cálculo de FFT utilizando la biblioteca de la cátedra
        FFTFrequency(SAMPLE_FREC, BUFFER_SIZE, f); //vector eje de frecuencias
        FFTMagnitude(buffer_filt2, espectro_filt_fft, BUFFER_SIZE); //espectro de la señal ya filtrada (por alto y por bajo)
    
        // 6. Búsqueda de la Frecuencia Dominante en el espectro filtrado
        float max_magnitud = 0.0f;
        uint16_t idx_max = 0;

        // Evaluar desde el bin 1 (~0.78 Hz) hasta N/2 (50 Hz, límite Nyquist)
        for (int16_t i = 1; i < (BUFFER_SIZE / 2); i++) {
            if (espectro_filt_fft[i] > max_magnitud) {
                max_magnitud = espectro_filt_fft[i];
                idx_max = i;
            }
        }
        frecuencia_dominante = f[idx_max];

        // Calculo de métricas acumuladas y envío de resultados por UART y BLE
        if (primera_ventana) {
            primera_ventana = false;   // descarta el transitorio
        } else {
        for (int i = 0; i < BUFFER_SIZE / 2; i++) espectro_acum[i] += espectro_filt_fft[i];
            rms2_acum += rms_temblor * rms_temblor;
            n_ventanas++;
        }
        // 7. Transmisión del espectro por UART y Bluetooth SPP (frecuencia y magnitud)
        // Envío en formato para Serial Plotter: >voltaje:VALOR\r\n
        char msg[32];
        // Borra el espectro anterior
        EnviarLinea("*GC*");
        for (int i = 1; i <= BIN_ENVIO_MAX; i++) {
            snprintf(msg, sizeof(msg), "*GX%.1fY%.2f*", f[i], espectro_filt_fft[i]);
            EnviarLinea(msg);
        }

        // 8. Transmisión de métricas finales (RMS y Frecuencia Dominante) para la App Movil
        snprintf(msg, sizeof(msg), "*F%.2f*", frecuencia_dominante);
        EnviarLinea(msg);
        snprintf(msg, sizeof(msg), "*R%.2f*", rms_temblor);
        EnviarLinea(msg);

        // 9. Transmisión limpia por UART exclusivamente para Serial Oscilloscope (PC)
        for (int i = 1; i <= BIN_ENVIO_MAX; i++) {
            char msg_pc[32];
            snprintf(msg_pc, sizeof(msg_pc), ">magnitud:%.2f\r\n", espectro_filt_fft[i]);
            UartSendString(UART_PC, msg_pc);
            vTaskDelay(5 / portTICK_PERIOD_MS); // Pequeña pausa para no saturar el buffer UART
        }
    }
}
/**
 * @brief Tarea FreeRTOS que gestiona el control de tiempo, la activación del buzzer y la secuencia general de las pruebas.
 * @param pvParameter Puntero a parámetros de tarea de FreeRTOS (no utilizado).
 */
static void PruebasTask(void *pvParameter){
    while(true){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if(estado_actual == MODO_ESTATICO){
            ReiniciarAcumuladores();
            ESP_LOGI(TAG, "--- PRUEBA ESTATICA INICIADA ---");
    
            // Tono corto indicando inicio
            BuzzerOn();
            vTaskDelay(200 / portTICK_PERIOD_MS);
            BuzzerOff();

            // Esperar el tiempo de la prueba mientras se evalúa el temblor
            vTaskDelay(TIEMPO_PRUEBA_ESTATICA_MS / portTICK_PERIOD_MS);

            estado_actual = MODO_REPOSO;
            EnviarResultadoFinal();

            // Tono largo indicando fin de la prueba
            BuzzerOn();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            BuzzerOff();

            ESP_LOGI(TAG, "--- PRUEBA ESTATICA FINALIZADA ---");
        }
        else if(estado_actual == MODO_DINAMICO){
            ReiniciarAcumuladores();
            ESP_LOGI(TAG, "--- PRUEBA DINAMICA INICIADA ---");

            // Tono corto indicando inicio
            BuzzerOn();
            vTaskDelay(200 / portTICK_PERIOD_MS);
            BuzzerOff();

            // Espera de tiempo fijo para la realización del movimiento dinámico
            vTaskDelay(10000 / portTICK_PERIOD_MS);

            estado_actual = MODO_REPOSO;
            EnviarResultadoFinal();

            // Tono continuo indicando finalización de la prueba dinámica
            BuzzerOn();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            BuzzerOff();

            ESP_LOGI(TAG, "--- PRUEBA DINAMICA FINALIZADA ---");
        }
    }
}
/*==================[external functions definition]==========================*/
/**
 * @brief Función principal del programa. Inicializa periféricos, filtros, tareas FreeRTOS y temporizador de adquisición.
 */
void app_main(void){
    // 1. Inicialización de la UART
    serial_config_t my_uart = {
        .port      = UART_PC,
        .baud_rate = 921600,
        .func_p    = NULL,
        .param_p   = NULL
    };
    UartInit(&my_uart);

    ESP_LOGI(TAG, "Inicializando periféricos...");

    // Inicializaciones de hardware y librerías de la cátedra
    LedsInit(); 
    SwitchesInit();
    BuzzerInit(GPIO_20);   //Pin asignado al Buzzer 
    FFTInit();  
    LowPassInit(SAMPLE_FREC, 15, ORDER_2); // Pasa bajas en 15 Hz (corta ruido térmico/movimiento rápido)
    HiPassInit(SAMPLE_FREC, 1, ORDER_2);   // Pasa altas en 1 Hz (elimina deriva lenta)
    
    // Inicialización del módulo Bluetooth SPP
    ble_config_t my_ble = {
    .device_name = "EVALUADOR_TEMBLOR_BLE",
    .func_p      = NULL
    };
    BleInit(&my_ble);

    // Inicializo los Switches de selección de prueba con interrupciones
    SwitchActivInt(SWITCH_1, &Tecla1_ISR, NULL);
    SwitchActivInt(SWITCH_2, &Tecla2_ISR, NULL);
    

    // Inicialización del acelerómetro analógico ADXL335
    if (ADXL335Init()) {
        ESP_LOGI(TAG, "ADXL335 inicializado con éxito.");
    } else {
        ESP_LOGE(TAG, "Error al inicializar ADXL335.");
    }

	// 3. Configuración del Timer A para el muestreo
    timer_config_t timer_Adquisicion = {
        .timer   = TIMER_A,
        .period  = CONFIG_BLINK_PERIOD_Adquisicion_US,
        .func_p  = FuncTimerA,
        .param_p = NULL
    };
    TimerInit(&timer_Adquisicion);

	// 4. Creación de las tareas de FreeRTOS
    xTaskCreate(&AdquisicionTask, "Adquisicion", 4096, NULL, 5, &Adquisicion_task_handle);
    xTaskCreate(&ProcesamientoTask, "Procesamiento", 4096, NULL, 5, &Procesamiento_task_handle);
    xTaskCreate(&PruebasTask, "Pruebas", 4096, NULL, 5, &Pruebas_task_handle);

    // 5. Inicio del Timer A para la adquisición de datos
    TimerStart(timer_Adquisicion.timer);

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
/*==================[end of file]============================================*/