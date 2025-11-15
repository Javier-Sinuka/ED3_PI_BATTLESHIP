#include "LPC17xx.h"
#include "battleship_max.h"

static volatile uint32_t g_msTicks = 0U;
static uint32_t lcg_state = 1234567u;

void SysTick_Handler(void){
    g_msTicks++;
    BS_AnimationsUpdate(g_msTicks);
}

uint32_t BS_Hal_GetMillis(void){
    return g_msTicks;
}

uint32_t BS_Hal_GetRandom(void){
    // LCG simple
    lcg_state = 1664525u * lcg_state + 1013904223u;
    return lcg_state;
}
