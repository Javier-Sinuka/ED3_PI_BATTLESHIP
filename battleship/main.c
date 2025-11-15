#include "LPC17xx.h"
#include "max_controll.h"
#include "battleship_max.h"
#include <stdint.h>

/* ===================== SysTick config (TU VERSIÓN) ===================== */

#define BIT_MASK(X)   (0x01u << (X))
#define CORE          100000u   // según tu definición
#define TICKS         1000u
#define ST_LOAD       ((TICKS * CORE) - 1u)

static void configSysTick(void) {
    SysTick->LOAD = ST_LOAD;
    SysTick->VAL  = 0u;
    SysTick->CTRL = 0x07u;   // ENABLE + TICKINT + CLKSOURCE (core)
    // El SysTick_Handler real está en hw_time.c (BS_AnimationsUpdate + millis)
}

/* ===================== Botones de prueba (PORT2) ===================== */

#define BTN_PORT       2u
#define BTN_LEFT_PIN   10u
#define BTN_RIGHT_PIN  11u

// Leer GPIO genérico (solo usamos puerto 2 acá)
static uint8_t readGPIO(uint8_t port, uint8_t pin) {
    if (port == 2u) {
        return ( (LPC_GPIO2->FIOPIN & (1u << pin)) != 0u );
    }
    return 1u;  // por defecto "no presionado"
}

// Inicializar botones como entradas con pull-up interno
static void TestButtons_Init(void) {
    // P2.10 y P2.11 como entrada
    LPC_GPIO2->FIODIR &= ~((1u << BTN_LEFT_PIN) | (1u << BTN_RIGHT_PIN));

    // Pull-up interno en P2.10-11  (PINMODE4, bits 20-23)
    // 00 = pull-up
    LPC_PINCON->PINMODE4 &= ~((3u << 20) | (3u << 22));
}

// Poner el resto de los pines de PORT2 en salida (para que no floten)
// OJO: si usás otros pines de P2 para otra cosa, ajustá la máscara.
static void GPIO_InitUnusedPins_Port2(void) {
    uint32_t mask_inputs = (1u << BTN_LEFT_PIN) | (1u << BTN_RIGHT_PIN);
    // Todos 1 salvo 10 y 11 -> esos quedan como entrada
    LPC_GPIO2->FIODIR |= ~mask_inputs;
}

/* ===================== Estado del bit en bloque 0 ===================== */

// Bit en bloque 0 (BS_DEV_PIVOT), fila fija 3, columna 0..7
static uint8_t test_row = 3u;
static uint8_t test_col = 0u;
static uint8_t pivot_rows[8];

// Para detectar flancos de los botones (pull-up => reposo = 1)
static uint8_t last_left  = 1u;
static uint8_t last_right = 1u;

// Actualiza el bit del bloque 0 según los botones
static void Test_UpdatePivotFromButtons(void) {
    uint8_t left_level  = readGPIO(BTN_PORT, BTN_LEFT_PIN);
    uint8_t right_level = readGPIO(BTN_PORT, BTN_RIGHT_PIN);

    // flanco de bajada = 1 -> 0 (botón presionado)
    if (left_level == 0u && last_left == 1u) {
        if (test_col > 0u) {
            test_col--;
        }
    }
    if (right_level == 0u && last_right == 1u) {
        if (test_col < 7u) {
            test_col++;
        }
    }

    last_left  = left_level;
    last_right = right_level;

    // Actualizar display del bloque 0
    {
        int r;
        for (r = 0; r < 8; r++) {
            pivot_rows[r] = 0u;
        }
        pivot_rows[test_row] |= (uint8_t)(1u << test_col);
        MAX_DrawRows(BS_DEV_PIVOT, pivot_rows);
    }
}

/* =============================== main =============================== */

int main(void) {
    int r;

    SystemInit();        // config básica de la LPC (PLL, etc.)
    configSysTick();     // tu SysTick (el handler está en hw_time.c)

    // Inicializar SPI0 para el MAX7219
    MAX_SPI0_Init();

    // Inicializar toda la lógica del juego (sin usarla todavía):
    //  - MAX_InitAll()
    //  - tablero oponente en bloque 2
    //  - contador en bloque 3
    //  - tablero jugador limpio, etc.
    BS_GameInit();

    // Inicializar botones de prueba y poner resto de P2 en salida
    TestButtons_Init();
    GPIO_InitUnusedPins_Port2();

    // Estado inicial del bit en bloque 0
    for (r = 0; r < 8; r++) {
        pivot_rows[r] = 0u;
    }
    pivot_rows[test_row] |= (uint8_t)(1u << test_col);
    MAX_DrawRows(BS_DEV_PIVOT, pivot_rows);

    while (1) {
        // Leer botones y actualizar el bit en el bloque 0
        Test_UpdatePivotFromButtons();
        // No hace falta delay: el parpadeo/animaciones del Battleship
        // las maneja BS_AnimationsUpdate en el SysTick_Handler (hw_time.c).
    }
}
