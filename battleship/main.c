#include "LPC17xx.h"
#include "max7219.h"
#include "battleship.h"
#include <stdint.h>

// ====== Pines de botones (pull-up interno) ======
#define BTN_PORT       2
#define BTN_LEFT_PIN   10
#define BTN_RIGHT_PIN  11

// ====== Helper lectura GPIO ======
static uint8_t readGPIO(uint8_t port, uint8_t pin){
    if (port == 0) return ( (LPC_GPIO0->FIOPIN & (1U << pin)) != 0U );
    if (port == 1) return ( (LPC_GPIO1->FIOPIN & (1U << pin)) != 0U );
    if (port == 2) return ( (LPC_GPIO2->FIOPIN & (1U << pin)) != 0U );
    if (port == 3) return ( (LPC_GPIO3->FIOPIN & (1U << pin)) != 0U );
    return 1U;
}

// ====== Inicialización de botones de test ======
static void TestButtons_Init(void){
    // P2.10 y P2.11 como entrada
    LPC_GPIO2->FIODIR &= ~((1U << BTN_LEFT_PIN) | (1U << BTN_RIGHT_PIN));

    // Pull-up interno en P2.10-11 (PINMODE4 bits 20-23)
    LPC_PINCON->PINMODE4 &= ~((3U << 20) | (3U << 22)); // 00 = pull-up
}

// ====== Estado local del bit de prueba en bloque 0 ======
static uint8_t test_row = 3U;  // fila fija
static uint8_t test_col = 0U;  // columna 0..7
static uint8_t pivot_rows[8];

// Flancos
static uint8_t last_left  = 1U; // 1 = HIGH = no presionado
static uint8_t last_right = 1U;

// ====== Función auxiliar: mover bit con 2 botones ======
static void Test_UpdatePivotFromButtons(void){
    uint8_t left_level  = readGPIO(BTN_PORT, BTN_LEFT_PIN);
    uint8_t right_level = readGPIO(BTN_PORT, BTN_RIGHT_PIN);

    // flanco de bajada = HIGH (1) -> LOW (0) = presionado
    if (left_level == 0U && last_left == 1U){
        if (test_col > 0U){
            test_col--;
        }
    }
    if (right_level == 0U && last_right == 1U){
        if (test_col < 7U){
            test_col++;
        }
    }

    last_left  = left_level;
    last_right = right_level;

    // Actualizar bloque 0
    {
        int r;
        for (r = 0; r < 8; r++){
            pivot_rows[r] = 0U;
        }
        pivot_rows[test_row] |= (uint8_t)(1u << test_col);
        MAX_DrawRows(BS_DEV_PIVOT, pivot_rows);
    }
}

// ====== Timer3: contador infinito 9->0->9->... ======
static void Timer3_Init_1Hz(void){
    // Habilitar clock Timer3
    LPC_SC->PCONP |= (1U << 23); // PCTIM3

    // PCLK para Timer3 = CCLK/4
    LPC_SC->PCLKSEL1 &= ~(3U << 14); // PCLK_TIMER3 = CCLK/4

    LPC_TIM3->TCR = 0x02; // reset
    LPC_TIM3->PR  = 0;    // prescaler = 0 (cuenta PCLK directamente)
    // Suponiendo CCLK = 100 MHz -> PCLK = 25 MHz -> 1s = 25e6 ticks
    LPC_TIM3->MR0 = 25000000U - 1U;
    LPC_TIM3->MCR = (1U << 0) | (1U << 1); // interrupt + reset
    LPC_TIM3->TCR = 0x01;                  // start

    NVIC_EnableIRQ(TIMER3_IRQn);
}

void TIMER3_IRQHandler(void){
    if (LPC_TIM3->IR & 1U){ // MR0 interrupt
        uint8_t finished = BS_CountdownStep();
        if (finished){
            // cuando llega a 0, lo reseteamos a 9 para que siga
            BS_CountdownSet(9U);
        }
        LPC_TIM3->IR = 1U; // limpiar flag
    }
}

// ====== main ======
int main(void){
    int r;

    SystemInit();

    // SysTick: 1 ms -> BS_Hal_GetMillis + BS_AnimationsUpdate
    SysTick_Config(SystemCoreClock / 1000);

    // SPI0 para MAX7219
    MAX_SPI0_Init();

    // Inicializar juego Battleship:
    //  - inicializa MAX7219
    //  - genera tablero oponente (bloque 2)
    //  - prepara tablero jugador, cursor, contador, etc.
    BS_GameInit();

    // Contador arrancando en 9 (luego Timer3 lo mueve y resetea)
    BS_CountdownSet(9U);
    Timer3_Init_1Hz();

    // Inicializar botones de prueba
    TestButtons_Init();

    // Estado inicial del bit en bloque 0
    for (r = 0; r < 8; r++){
        pivot_rows[r] = 0U;
    }
    pivot_rows[test_row] |= (uint8_t)(1u << test_col);
    MAX_DrawRows(BS_DEV_PIVOT, pivot_rows);

    while (1){
        // Leer botones y actualizar el bit en bloque 0
        Test_UpdatePivotFromButtons();
        // Podrías agregar __WFI(); si querés bajar consumo
        // __WFI();
    }
}
