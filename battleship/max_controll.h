#ifndef MAX_CONTROLL_H
#define MAX_CONTROLL_H

#include <stdint.h>

// Número de MAX7219 en cascada
#define MAX_NUM_DEVICES  4

// IDs lógicos de cada bloque
#define BS_DEV_PIVOT      0   // Bloque 0: cursor / bit de prueba
#define BS_DEV_PLAYER     1   // Bloque 1: tablero jugador
#define BS_DEV_OPPONENT   2   // Bloque 2: tablero oponente
#define BS_DEV_COUNTER    3   // Bloque 3: contador

// Registros internos
#define MAX_REG_NOOP        0x00
#define MAX_REG_DIGIT0      0x01
#define MAX_REG_DECODE_MODE 0x09
#define MAX_REG_INTENSITY   0x0A
#define MAX_REG_SCAN_LIMIT  0x0B
#define MAX_REG_SHUTDOWN    0x0C
#define MAX_REG_DISPLAYTEST 0x0F

// ==== HAL para LPC1769 ====

// Llevar CS a nivel activo
void MAX_HAL_Select(void);
// Llevar CS a nivel inactivo
void MAX_HAL_Deselect(void);
// Enviar un byte por SPI0
void MAX_HAL_SendByte(uint8_t data);

// Inicializar SPI0 (pines + registros) para el MAX7219
void MAX_SPI0_Init(void);

// ==== API de alto nivel del MAX7219 ====

void MAX_InitDevice(uint8_t dev);
void MAX_InitAll(void);
void MAX_Clear(uint8_t dev);
void MAX_SetRow(uint8_t dev, uint8_t row, uint8_t val);
void MAX_DrawRows(uint8_t dev, const uint8_t rows[8]);

#endif
