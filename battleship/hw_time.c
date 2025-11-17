#include "LPC17xx.h"
#include "battleship_max.h"

static volatile uint32_t g_msTicks = 0u;
static uint32_t g_lcg_state = 1234567u;

// SysTick: se llama según tu configSysTick()
void SysTick_Handler(void) {
    g_msTicks++;                      // “ms” lógicos, no hace falta que sea exacto
    BS_AnimationsUpdate(g_msTicks);   // la librería maneja el blink internamente
}

uint32_t BS_Hal_GetMillis(void) {
    return g_msTicks;
}

uint32_t BS_Hal_GetRandom(void) {
    g_lcg_state = 1664525u * g_lcg_state + 1013904223u;
    return g_lcg_state;
}
