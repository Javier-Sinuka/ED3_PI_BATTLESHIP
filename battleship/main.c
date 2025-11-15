#include "LPC17xx.h"
#include "max_controll.h"
#include <stdint.h>

/* ===================== SysTick config (tuyo) ===================== */

#define BIT_MASK(X)   (0x01u << (X))
#define CORE          100000u   // según tu proyecto
#define TICKS         1000u     // según tu proyecto
#define ST_LOAD       ((TICKS * CORE) - 1u)

void configSysTick(void) {
    SysTick->LOAD = ST_LOAD;
    SysTick->VAL  = 0;
    SysTick->CTRL = 0x07;   // ENABLE + TICKINT + CLKSOURCE (core)
}

/* Contador de ticks de SysTick */
static volatile uint32_t g_ticks = 0u;

/* Handler de SysTick: se llama cada vez que se vence ST_LOAD */
void SysTick_Handler(void) {
    g_ticks++;
}

/* Delay en "ticks de SysTick" (no en ms) */
static void delayTicks(uint32_t ticks) {
    uint32_t start = g_ticks;
    // Espera bloqueante hasta que pasen "ticks" interrupciones de SysTick
    while ((uint32_t)(g_ticks - start) < ticks) {
        // spin
    }
}

/* ===================== Tabla de dígitos (0–9) ===================== */

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

/* Dibuja un dígito en un MAX7219 (dev = 0..3) */
static void drawDigitOnDevice(uint8_t dev, uint8_t num) {
    uint8_t r;
    if (num > 9u) return;

    // Misma convención que en la librería: invertimos filas (0 abajo)
    for (r = 0; r < 8u; r++) {
        MAX_SetRow(dev, (uint8_t)(7u - r), digits[num][r]);
    }
}

/* ============================ main ============================ */

int main(void) {
    uint8_t up0   = 0u;  // bloque 0: 0→9→0→...
    uint8_t up2   = 0u;  // bloque 2: 0→9→0→...
    uint8_t down1 = 9u;  // bloque 1: 9→0→9→...
    uint8_t down3 = 9u;  // bloque 3: 9→0→9→...

    SystemInit();

    // Configurar SysTick con tu función
    configSysTick();

    // Inicializar SPI0 para MAX7219 (de tu librería)
    MAX_SPI0_Init();
    // Inicializar los 4 MAX7219 (intensidad, scan limit, etc.)
    MAX_InitAll();

    while (1) {
        // Bloque 0 (dev 0) y Bloque 2 (dev 2): contadores ascendentes
        drawDigitOnDevice(BS_DEV_PIVOT,    up0);  // dev 0
        drawDigitOnDevice(BS_DEV_OPPONENT, up2);  // dev 2

        // Bloque 1 (dev 1) y Bloque 3 (dev 3): contadores descendentes
        drawDigitOnDevice(BS_DEV_PLAYER,   down1); // dev 1
        drawDigitOnDevice(BS_DEV_COUNTER,  down3); // dev 3

        // Espera un tiempo visible (ajustá el valor según tus macros CORE/TICKS)
        delayTicks(1u);  // 1 tick de SysTick; si es muy rápido/lento, ajustá a gusto

        // Actualizar contadores
        up0 = (uint8_t)((up0 + 1u) % 10u);
        up2 = (uint8_t)((up2 + 1u) % 10u);

        if (down1 == 0u) down1 = 9u;
        else             down1--;

        if (down3 == 0u) down3 = 9u;
        else             down3--;
    }
}
