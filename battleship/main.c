#include "LPC17xx.h"
#include "library_bs/max_controll.h"
#include "library_bs/battleship_max.h"
#include <stdbool.h>
#include <stdint.h>

// ====== Pines de botones (pull-up interno) ======
#define BTN_PORT   2
#define BTN_LEFT_PIN   10
#define BTN_RIGHT_PIN  11

// ====== Helper lectura GPIO ======
static inline bool readGPIO(uint8_t port, uint8_t pin){
    if (port == 0) return (LPC_GPIO0->FIOPIN & (1U << pin)) != 0;
    if (port == 1) return (LPC_GPIO1->FIOPIN & (1U << pin)) != 0;
    if (port == 2) return (LPC_GPIO2->FIOPIN & (1U << pin)) != 0;
    if (port == 3) return (LPC_GPIO3->FIOPIN & (1U << pin)) != 0;
    return true;
}

// ====== Inicialización de botones de test ======
static void TestButtons_Init(void){
    // P2.10 y P2.11 como entrada
    LPC_GPIO2->FIODIR &= ~((1U << BTN_LEFT_PIN) | (1U << BTN_RIGHT_PIN));

    // Pull-up interno en P2.10-11 (PINMODE4 bits 20-23)
    LPC_PINCON->PINMODE4 &= ~((3U << 20) | (3U << 22)); // 00 = pull-up
}

// ====== Estado local del bit de prueba en bloque 0 ======
static uint8_t test_row = 3;  // fila fija
static uint8_t test_col = 0;  // columna que se moverá 0..7
static uint8_t pivot_rows[8];

// Para detectar flancos
static bool last_left  = true; // HIGH = no presionado
static bool last_right = true;

// ====== Función auxiliar pedida: mover bit con 2 botones ======
static void Test_UpdatePivotFromButtons(void){
    bool left_level  = readGPIO(BTN_PORT, BTN_LEFT_PIN);   // HIGH=no pres
    bool right_level = readGPIO(BTN_PORT, BTN_RIGHT_PIN);

    // flanco de bajada = pasó de HIGH -> LOW = botón presionado
    if (left_level == false && last_left == true){
        // mover a la izquierda si se puede
        if (test_col > 0){
            test_col--;
        }
    }
    if (right_level == false && last_right == true){
        // mover a la derecha si se puede
        if (test_col < 7){
            test_col++;
        }
    }

    last_left  = left_level;
    last_right = right_level;

    // Actualizar bloque 0 (BS_DEV_PIVOT)
    for (int r = 0; r < 8; r++){
        pivot_rows[r] = 0;
    }
    pivot_rows[test_row] |= (uint8_t)(1u << test_col);
    MAX_DrawRows(BS_DEV_PIVOT, pivot_rows);
}

// ====== Timer3: contador 9→0 y reset ======
static void Timer3_Init_1Hz(void){
    // Habilitar clock Timer3
    LPC_SC->PCONP |= (1U << 23); // PCTIM3

    // PCLK para Timer3 = CCLK/4 (por ejemplo)
    LPC_SC->PCLKSEL1 &= ~(3U << 14); // PCLK_TIMER3 = CCLK/4

    LPC_TIM3->TCR = 0x02; // reset
    LPC_TIM3->PR  = 0;    // prescaler = 0 (cuenta directamente ticks de PCLK)
    // Supongamos CCLK = 100 MHz, PCLK = 25 MHz -> para 1s, MR0 = 25.000.000
    LPC_TIM3->MR0 = 25000000 - 1;
    LPC_TIM3->MCR = (1U << 0) | (1U << 1); // interrupt + reset on MR0
    LPC_TIM3->TCR = 0x01;                  // start

    NVIC_EnableIRQ(TIMER3_IRQn);
}

void TIMER3_IRQHandler(void){
    if (LPC_TIM3->IR & 1U){ // MR0 interrupt
        bool finished = BS_CountdownStep();
        if (finished){
            // cuando llega a 0, lo reseteamos a 9 para que siga
            BS_CountdownSet(9);
        }
        LPC_TIM3->IR = 1U; // limpiar flag
    }
}

// ====== main ======
int main(void){
    SystemInit();

    // SysTick: 1 ms → BS_Hal_GetMillis + BS_AnimationsUpdate
    SysTick_Config(SystemCoreClock / 1000);

    // SPI0 para MAX7219
    MAX_SPI0_Init();
    MAX_InitAll();

    // Inicializar juego Battleship:
    // - Genera tablero oponente (bloque 2)
    // - Prepara tablero jugador, cursor, contador, etc.
    BS_GameInit();

    // Contador en bloque3 arrancando en 9 (luego Timer3 lo mueve)
    BS_CountdownSet(9);
    Timer3_Init_1Hz();

    // Inicializar botones de prueba
    TestButtons_Init();

    // Estado inicial del bit en bloque 0
    for (int r = 0; r < 8; r++){
        pivot_rows[r] = 0;
    }
    pivot_rows[test_row] |= (uint8_t)(1u << test_col);
    MAX_DrawRows(BS_DEV_PIVOT, pivot_rows);

    while (1){
        // En cada iteración, leo botones y actualizo el bit en bloque 0
        Test_UpdatePivotFromButtons();
        // No hace falta nada más:
        // - bloque2 se actualiza con BS_AnimationsUpdate (desde SysTick)
        // - bloque3 baja por TIMER3 y se resetea cuando llega a 0
        // Podés agregar __WFI() si querés ahorrar energía:
        // __WFI();
    }
}
