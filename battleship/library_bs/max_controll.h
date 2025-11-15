#ifndef MAX_CONTROLLER_H
#define MAX_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Librería de manejo genérico del MAX7219 para N matrices en cascada.
 * La capa HAL de SPI/CS debe ser implementada
 * por el usuario (ver prototipos MAX_HAL_* abajo).
 */

// Número de dispositivos en cascada (Nuestro Max tiene 4)
#define MAX_NUM_DEVICES  4

// ==== ID de cada MAX7219 del sistema ====
#define BS_DEV_PIVOT      0   // Bloque 0: cursor de barco / cursor disparos
#define BS_DEV_PLAYER     1   // Bloque 1: tablero del jugador
#define BS_DEV_OPPONENT   2   // Bloque 2: tablero del oponente
#define BS_DEV_COUNTER    3   // Bloque 3: display de números

// Registros internos del MAX7219
#define MAX_REG_NOOP        0x00
#define MAX_REG_DIGIT0      0x01
#define MAX_REG_DECODE_MODE 0x09
#define MAX_REG_INTENSITY   0x0A
#define MAX_REG_SCAN_LIMIT  0x0B
#define MAX_REG_SHUTDOWN    0x0C
#define MAX_REG_DISPLAYTEST 0x0F

//================== HAL A IMPLEMENTAR POR EL USUARIO ==================
/*
 * Estas funciones deben existir en el proyecto de la LPC.
 * La librería max7219.c las usa internamente.
 */

// Llevar la línea CS del MAX7219 a nivel activo (LOW normalmente).
void MAX_HAL_Select(void);

// Llevar CS a nivel inactivo (HIGH).
void MAX_HAL_Deselect(void);

// Enviar un byte por SPI hacia la cadena de MAX7219.
void MAX_HAL_SendByte(uint8_t data);

//=====================================================================

/*
 * Inicializa un MAX7219 específico (dev 0..MAX_NUM_DEVICES-1).
 * - Sale de test
 * - scan limit (8 filas)
 * - sin decode
 * - display on
 * - intensidad moderada
 * - clear
 */
void MAX_InitDevice(uint8_t dev);

/*
 * Inicializa todos los MAX7219 de la cadena.
 */
void MAX_InitAll(void);

/*
 * Limpia (apaga) todas las filas de un dispositivo.
 */
void MAX_Clear(uint8_t dev);

/*
 * Escribe una fila completa en un dispositivo (row 0..7).
 * `val` es un byte de columnas, bit = LED.
 * La función internamente invierte los bits para corregir el cableado.
 */
void MAX_SetRow(uint8_t dev, uint8_t row, uint8_t val);

/*
 * Dibuja 8 filas en el dispositivo indicado a partir de `rows[8]`.
 */
void MAX_DrawRows(uint8_t dev, const uint8_t rows[8]);

#endif
