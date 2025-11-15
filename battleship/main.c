#include "LPC17xx.h"
#include "max7219.h"
#include <stdint.h>

// =================== SysTick: delay en ms ===================

static volatile uint32_t g_msTicks = 0U;

void SysTick_Handler(void) {
    g_msTicks++;
}

static void delayMs(uint32_t ms) {
    uint32_t target = g_msTicks + ms;
    // cuidado con overflow, uso resta signed
    while ((int32_t)(target - g_msTicks) > 0) {
        // espera bloqueante
    }
}

// =================== Patrones de dígitos (0–9) ===================
// Mismo formato que en battleship.c, pero lo traemos acá para usarlo
// directamente desde el main.

static const uint8_t digits[10][8] = {
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x66,0x3C}, //0
    {0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x7E}, //1
    {0x3C,0x66,0x06,0x0C,0x18,0x60,0x66,0x7E}, //2
    {0x3C,0x66,0x06,0x1C,0x06,0x06,0x66,0x3C}, //3
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x1E}, //4
    {0x7E,0x60,0x7C,0x06,0x06,0x06,0x66,0x3C}, //5
    {0x3C,0x66,0x60,0x7C,0x66,0x66,0x66,0x3C}, //6
    {0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x18}, //7
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x66,0x3C}, //8
    {0x3C,0x66,0x66,0x3E,0x06,0x06,0x66,0x3C}  //9
};

// Dibuja un dígito en un dispositivo MAX concreto (0..3)
static void drawDigitOnDevice(uint8_t dev, uint8_t num) {
    uint8_t r;
    if (num > 9U) return;

    // Usamos mismo criterio que en battleship: fila lógica 0 abajo -> row 7
    for (r = 0; r < 8; r++) {
        MAX_SetRow(dev, (uint8_t)(7U - r), digits[num][r]);
    }
}

// =================== main ===================

int main(void) {
    uint8_t up0  = 0U;  // bloque 0 sube 0→9
    uint8_t up2  = 0U;  // bloque 2 sube 0→9
    uint8_t down1 = 9U; // bloque 1 baja 9→0
    uint8_t down3 = 9U; // bloque 3 baja 9→0

    SystemInit();

    // SysTick a 1 kHz para delayMs()
    SysTick_Config(SystemCoreClock / 1000U);

    // Inicializar SPI0 para MAX7219
    MAX_SPI0_Init();
    // Inicializar los 4 MAX (intensidad, scan limit, etc.)
    MAX_InitAll();

    while (1) {
        // Bloque 0 y 2: contadores crecientes
        drawDigitOnDevice(BS_DEV_PIVOT,   up0);  // dev 0
        drawDigitOnDevice(BS_DEV_OPPONENT, up2); // dev 2

        // Bloque 1 y 3: contadores decrecientes
        drawDigitOnDevice(BS_DEV_PLAYER,  down1); // dev 1
        drawDigitOnDevice(BS_DEV_COUNTER, down3); // dev 3

        // Espera ~300 ms entre cambios (ajustá a gusto)
        delayMs(300U);

        // Actualizar contadores
        up0 = (uint8_t)((up0  + 1U) % 10U);
        up2 = (uint8_t)((up2  + 1U) % 10U);

        if (down1 == 0U) down1 = 9U;
        else             down1--;

        if (down3 == 0U) down3 = 9U;
        else             down3--;
    }
}
