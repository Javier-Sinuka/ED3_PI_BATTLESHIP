// battleship_max.h
#ifndef BATTLESHIP_MAX_H
#define BATTLESHIP_MAX_H

#include <stdint.h>
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

// ===== Modo interno de juego =====
typedef enum {
    MODE_PLACE = 0,    // colocando barcos (P1 o P2)
    MODE_SHOT  = 1,    // disparando (P1 o P2)
    MODE_GAME_OVER = 2 // juego terminado
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
    BS_PLACE_ALL_DONE       // se colocó último (2,4,6)
} BS_PlaceResult;

// ==== HAL mínima requerida ====
uint32_t BS_Hal_GetMillis(void);
uint32_t BS_Hal_GetRandom(void);

// ==== API de inicialización ====
void BS_GameInit(void);
BS_Mode BS_GetMode(void);

// ==== API de animaciones (blink, etc.) ====
// Llamar periódicamente (p.ej. desde SysTick_Handler).
void BS_AnimationsUpdate(uint32_t nowMs);

// ==== API de contador (bloque 3) ====
void BS_CountdownSet(uint8_t startValue);
// Devuelve 1 si llegó a 0, 0 en caso contrario
uint8_t BS_CountdownStep(void);

// ==== API de FASE DE COLOCACIÓN (bloque 0 + bloque 1) ====
//
// Cursor = barco "en preview" (2,4,6 celdas) sobre dev0, sumado
// al tablero fijo del jugador en dev1.

// Mueve cursor de colocación; respeta bordes, no chequea solapamiento.
// Devuelve 1 si se movió, 0 si no.
uint8_t BS_Placement_MoveCursor(BS_Dir dir);

// Intenta rotar barco H<->V; solo si entra en los bordes.
// Devuelve 1 si rotó, 0 si no.
uint8_t BS_Placement_RotateCursor(void);

// Intenta colocar el barco actual en la posición del cursor.
// (Se usa internamente por BS_OnConfirmButton, pero la dejamos pública.)
BS_PlaceResult BS_Placement_TryPlaceCurrentShip(uint32_t nowMs);

// ==== API de transición y confirmación de acción ====
//
// Esta función se llama en el botón principal:
//  - En fase de colocación: coloca barcos y avanza de P1->P2->fase de disparos.
//  - En fase de disparos: realiza el disparo, cambia de jugador o termina el juego.
void BS_OnConfirmButton(uint32_t nowMs);

// ==== API de FASE DE DISPARO (bloque 0 + bloque 2) ====
//
// Cursor = 1 bit en dev0; tablero oponente visible en dev2.

// Mueve cursor de disparo; respeta bordes.
// Devuelve 1 si se movió, 0 si no.
uint8_t BS_Shot_MoveCursor(BS_Dir dir);

// Dispara al oponente en la posición del cursor.
// (Se usa dentro de BS_OnConfirmButton normalmente)
SHOT_RESULT_t BS_Shot_FireAtCursor(void);

// Devuelve 1 si todos los barcos del oponente (del jugador activo) fueron destruidos.
uint8_t BS_Shot_OpponentAllDestroyed(void);

#endif // BATTLESHIP_MAX_H
