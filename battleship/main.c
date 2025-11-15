#include "LPC17xx.h"
#include "max_controll.h"
#include "battleship_max.h"
#include <stdint.h>

/* ===================== SysTick config (1 s) ===================== */

#define BIT_MASK(X)   (0x01u << (X))
#define CORE          100000u      // según tu proyecto
#define TICKS         1000u
#define ST_LOAD       ((TICKS * CORE) - 1u)

// Dígito actual en bloque 3 (decrementa cada 1 s)
static volatile uint8_t g_counter_digit = 9u;

// Tabla de dígitos (misma que en battleship)
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

static void drawDigitBlock3(uint8_t num) {
    uint8_t r;
    if (num > 9u) return;
    // mismo criterio que la lib: fila lógica 0 abajo -> row 7 del MAX
    for (r = 0; r < 8u; r++) {
        MAX_SetRow(BS_DEV_COUNTER, (uint8_t)(7u - r), digits[num][r]);
    }
}

static void configSysTick(void) {
    SysTick->LOAD = ST_LOAD;
    SysTick->VAL  = 0u;
    SysTick->CTRL = 0x07u;   // ENABLE + TICKINT + CLKSOURCE(core)
}

// SysTick cada 1 s: decrementa el dígito de bloque 3
void SysTick_Handler(void) {
    if (g_counter_digit == 0u) {
        g_counter_digit = 9u;
    } else {
        g_counter_digit--;
    }
    drawDigitBlock3(g_counter_digit);
}

/* ===================== Botones en GPIO (P2.10 / P2.11) ===================== */

#define BTN_PORT       2u
#define BTN_LEFT_PIN   10u
#define BTN_RIGHT_PIN  11u

// Bit en bloque 0: fila fija 3, columna 0..7
static volatile uint8_t g_test_row = 3u;
static volatile uint8_t g_test_col = 0u;
static uint8_t          g_pivot_rows[8];

// Redibuja el bit en bloque 0 según g_test_row / g_test_col
static void redrawPivotBlock(void) {
    int r;
    for (r = 0; r < 8; r++) {
        g_pivot_rows[r] = 0u;
    }
    g_pivot_rows[g_test_row] |= (uint8_t)(1u << g_test_col);
    MAX_DrawRows(BS_DEV_PIVOT, g_pivot_rows);
}

// Configura P2.10 y P2.11 como entradas con pull-up interno
// y habilita interrupciones por flanco de bajada en esos pines.
static void GPIO_Buttons_Init(void) {
    // P2.10 y P2.11 como entrada
    LPC_GPIO2->FIODIR &= ~((1u << BTN_LEFT_PIN) | (1u << BTN_RIGHT_PIN));

    // Pull-up interno en P2.10-11  (PINMODE4 bits 20-23)
    LPC_PINCON->PINMODE4 &= ~((3u << 20) | (3u << 22)); // 00 = pull-up

    // Habilitar interrupción por flanco de bajada en P2.10 y P2.11
    LPC_GPIOINT->IO2IntEnF |= (1u << BTN_LEFT_PIN) | (1u << BTN_RIGHT_PIN);
    // Limpiar flags previos
    LPC_GPIOINT->IO2IntClr  = (1u << BTN_LEFT_PIN) | (1u << BTN_RIGHT_PIN);

    // Habilitar EINT3 en NVIC
    NVIC_EnableIRQ(EINT3_IRQn);
}

// Poner el resto de los pines de PORT2 en salida para que no floten
static void GPIO_Port2_UnusedAsOutput(void) {
    uint32_t mask_inputs = (1u << BTN_LEFT_PIN) | (1u << BTN_RIGHT_PIN);
    LPC_GPIO2->FIODIR |= ~mask_inputs;
}

// EINT3: interrupción de GPIO (PORT0/2). Acá usamos PORT2.
void EINT3_IRQHandler(void) {
    uint32_t statF = LPC_GPIOINT->IO2IntStatF;

    // Botón izquierda (P2.10) presionado (flanco de bajada)
    if (statF & (1u << BTN_LEFT_PIN)) {
        if (g_test_col > 0u) {
            g_test_col--;
        }
        redrawPivotBlock();
        // Limpiar flag
        LPC_GPIOINT->IO2IntClr = (1u << BTN_LEFT_PIN);
    }

    // Botón derecha (P2.11) presionado (flanco de bajada)
    if (statF & (1u << BTN_RIGHT_PIN)) {
        if (g_test_col < 7u) {
            g_test_col++;
        }
        redrawPivotBlock();
        // Limpiar flag
        LPC_GPIOINT->IO2IntClr = (1u << BTN_RIGHT_PIN);
    }
}

/* =============================== main =============================== */

int main(void) {
    int r;

    SystemInit();

    // Configurar SysTick para 1 s (según CORE/TICKS)
    configSysTick();

    // Inicializar SPI0 para MAX7219
    MAX_SPI0_Init();

    // Inicializar juego (librería completa):
    //  - MAX_InitAll()
    //  - Tablero del jugador vacío (bloque 1)
    //  - Tablero del oponente con barcos random (bloque 2)
    //  - Contador inicial (bloque 3, normalmente 9)
    //  - Pivote inicial (bloque 0) -> lo vamos a sobrescribir
    BS_GameInit();

    // Inicializar contenedor del dígito del bloque 3
    g_counter_digit = 9u;
    drawDigitBlock3(g_counter_digit);   // nos aseguramos que arranque en 9

    // Inicializar matriz de pivote (bloque 0) en fila 3, col 0
    for (r = 0; r < 8; r++) {
        g_pivot_rows[r] = 0u;
    }
    g_pivot_rows[g_test_row] |= (uint8_t)(1u << g_test_col);
    MAX_DrawRows(BS_DEV_PIVOT, g_pivot_rows);

    // Configurar botones y resto de PORT2
    GPIO_Buttons_Init();
    GPIO_Port2_UnusedAsOutput();

    // Bucle principal vacío: todo se hace por interrupciones
    while (1) {
        // Podrías poner __WFI(); si querés ahorrar energía:
        // __WFI();
    }
}
