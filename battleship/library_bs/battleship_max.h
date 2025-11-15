#ifndef BATTLESHIP_H
#define BATTLESHIP_H

#include <stdint.h>
#include <stdbool.h>
#include "max_controll.h"

// ===== Estados de celda =====
typedef enum {
    WATER = 0,
    SHIP,
    HIT,
    MISS
} BATTLESHIP_STATUS_Type;

// ===== Orientación de barco =====
typedef enum {
    ORI_H = 0,
    ORI_V = 1
} ORI_t;

// ===== Modo interno de juego (solo informativo) =====
typedef enum {
    MODE_PLACE = 0,   // colocando barcos
    MODE_SHOT  = 1    // disparando
} BS_Mode;

// ===== Resultado de disparo =====
typedef enum {
    SHOT_NONE = 0,
    SHOT_MISS_RES,
    SHOT_HIT_RES,
    SHOT_REPEAT
} SHOT_RESULT_t;

// ===== Direcciones para movimientos =====
typedef enum {
    BS_DIR_LEFT = 0,
    BS_DIR_RIGHT,
    BS_DIR_UP,
    BS_DIR_DOWN
} BS_Dir;

// ===== Resultado de intento de colocar barco =====
typedef enum {
    BS_PLACE_OK = 0,        // se colocó barco actual
    BS_PLACE_INVALID,       // no se pudo (fuera de rango o solapado)
    BS_PLACE_ALL_DONE       // se colocó último (2,4,6) => todos listos
} BS_PlaceResult;

// ==== HAL mínima requerida ====
// Tiempo en ms (SysTick por ejemplo)
uint32_t BS_Hal_GetMillis(void);
// Random de 32 bits (para barcos del oponente)
uint32_t BS_Hal_GetRandom(void);

// ==== API de inicialización ====

// Inicializa todo el estado del juego y los 4 MAX7219.
// - Limpia tableros
// - Genera barcos aleatorios del oponente (2,4,6)
// - Modo inicial: colocación (MODE_PLACE)
// - Cursor de barco en fila 3, col 0
void BS_GameInit(void);

// Devuelve el modo actual (solo por si querés consultarlo).
BS_Mode BS_GetMode(void);

// ==== API de animaciones (blink, etc.) ====

// Debe llamarse periódicamente (p.ej. desde SysTick o Timer).
// Maneja:
//  - Blink del tablero del jugador cuando terminó de colocar todos los barcos.
//  - Blink de los MISSES del oponente.
//  - Blink de error cuando se intentó colocar un barco en posición inválida.
void BS_AnimationsUpdate(uint32_t nowMs);

// ==== API de contador (bloque 3) ====

// Setea el valor inicial del contador (0..9) y lo dibuja en el bloque 3.
void BS_CountdownSet(uint8_t startValue);

// Decrementa el contador en 1 (si >0) y actualiza display.
// Devuelve true si llegó a 0 (ya no sigue bajando más).
bool BS_CountdownStep(void);

// ==== API de FASE DE COLOCACIÓN (bloque 0 + bloque 1) ====
//
// IMPORTANTE: estas funciones se usan solamente mientras el modo
// es MODE_PLACE. No hacen nada si ya estás en modo de disparo.
//
// Cursor de colocación = barco "en preview" (2,4,6 celdas) sobre dev0,
// superpuesto al tablero fijo del jugador (dev1).

// Mueve el cursor de colocación (barco actual) una celda en la
// dirección indicada. Respeta bordes, pero NO chequea solapamiento.
// Devuelve true si se movió, false si no pudo.
bool BS_Placement_MoveCursor(BS_Dir dir);

// Intenta rotar el barco de horizontal <-> vertical.
// Solo se rota si sigue entrando dentro de los bordes.
// Devuelve true si se rotó, false si no se pudo.
bool BS_Placement_RotateCursor(void);

// Intenta colocar el barco actual en la posición del cursor.
// - Si no entra o se solapa con otro barco:
//   * NO bloquea.
//   * Dispara un "blink de error" manejado por BS_AnimationsUpdate().
//   * Devuelve BS_PLACE_INVALID.
// - Si se coloca bien:
//   * Copia SHIP al tablero del jugador.
//   * Avanza al siguiente barco (2 -> 4 -> 6).
//   * Devuelve BS_PLACE_OK, o BS_PLACE_ALL_DONE si era el último.
//
// Debés pasarle el tiempo actual (ms) para marcar el inicio del blink de error.
BS_PlaceResult BS_Placement_TryPlaceCurrentShip(uint32_t nowMs);

// ==== API de transición a FASE DE DISPARO ====

// Llamar UNA vez cuando ya colocaste todos los barcos y querés pasar
// al modo de disparo. Esto:
//  - Apaga el blink del tablero del jugador.
//  - Deja fijo dev1 con los barcos.
//  - Pone un cursor de 1 bit en dev0 (fila 3, col 0).
//  - Cambia mode = MODE_SHOT.
void BS_EnterShotMode(void);

// ==== API de FASE DE DISPARO (bloque 0 + bloque 2) ====
//
// Cursor de disparo = 1 bit en dev0, tablero del oponente se ve en dev2
// (para debug). BS_AnimationsUpdate se encarga de blink de MISSES.

// Mueve el cursor de disparo una celda en la dirección dada (solo bordes).
// Devuelve true si se movió, false si no pudo (o si no estás en MODE_SHOT).
bool BS_Shot_MoveCursor(BS_Dir dir);

// Dispara al oponente en la posición del cursor actual.
// Actualiza dev2 y estado (WATER->MISS, SHIP->HIT).
// Devuelve:
//  - SHOT_HIT_RES   si pegó en barco,
//  - SHOT_MISS_RES  si agua,
//  - SHOT_REPEAT    si ya estaba HIT/MISS.
SHOT_RESULT_t BS_Shot_FireAtCursor(void);

// Devuelve true si TODOS los barcos del oponente fueron destruidos
// (no quedan celdas SHIP).
bool BS_Shot_OpponentAllDestroyed(void);

#endif // BATTLESHIP_H
