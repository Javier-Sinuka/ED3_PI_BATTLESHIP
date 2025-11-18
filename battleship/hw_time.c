#include "LPC17xx.h"
#include "battleship_max.h"

// ================= Config SysTick =================

// Estos son los defines que vos usabas
#define CORE       100000u
#define TICKS      1000u
#define ST_LOAD    ((TICKS * CORE) - 1u)

/**
 * @brief Configura el SysTick para generar interrupciones periódicas.
 *
 * Con estos valores:
 *  - LOAD = ST_LOAD
 *  - VAL  = 0
 *  - CTRL = ENABLE | TICKINT | CLKSOURCE (0x07)
 *
 * La frecuencia exacta depende de cómo tengas configurado el clock del core,
 * pero para la lógica de blink solo necesitamos un "tick" estable,
 * no una base de tiempo perfecta en ms.
 */
void configSysTick(void) {
    SysTick->LOAD = ST_LOAD;
    SysTick->VAL  = 0u;
    SysTick->CTRL = 0x07u;   // ENABLE + TICKINT + CLKSOURCE
}

// ================= HAL de tiempo para battleship_max =================

static volatile uint32_t g_msTicks = 0u;
static uint32_t g_lcg_state = 1234567u;

/**
 * @brief Handler de SysTick.
 *
 * Se llama cada vez que expira el contador que configuramos en configSysTick().
 * Aumenta un contador de "ticks" y se lo pasa a la librería para
 * que actualice las animaciones (blink, etc.).
 */
void SysTick_Handler(void) {
    g_msTicks++;                      // “ms” lógicos (o ticks)
    BS_AnimationsUpdate(g_msTicks);   // la librería maneja el parpadeo según este tiempo
}

/**
 * @brief Devuelve el contador de tiempo lógico usado por la librería.
 */
uint32_t BS_Hal_GetMillis(void) {
    return g_msTicks;
}

/**
 * @brief Generador simple de números pseudo-aleatorios para battleship_max.
 */
uint32_t BS_Hal_GetRandom(void) {
    g_lcg_state = 1664525u * g_lcg_state + 1013904223u;
    return g_lcg_state;
}