// hw_time.c
#include "LPC17xx.h"
#include "battleship_max.h"

static volatile uint32_t g_msTicks = 0u;
static uint32_t g_lcg_state = 1234567u;

// SysTick: se llama cada vez que des tu configSysTick() (CORE/TICKS/ST_LOAD)
void SysTick_Handler(void) {
    g_msTicks++;                      // 1 tick de tiempo (no tiene por qué ser 1 ms real)
    BS_AnimationsUpdate(g_msTicks);   // la librería decide cómo blinkear según este tiempo
}

uint32_t BS_Hal_GetMillis(void) {
    return g_msTicks;
}

uint32_t BS_Hal_GetRandom(void) {
    g_lcg_state = 1664525u * g_lcg_state + 1013904223u;
    return g_lcg_state;
}
