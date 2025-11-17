#include "LPC17xx.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_adc.h"

#include "max_controll.h"
#include "battleship_max.h"

// ================= Macros base =================

#define BIT(x)          (1u << (x))

// Ratio del ADC (como en tu código)
#define ADC_RATE        180000u

// Límites de movimiento del joystick
#define UPPER_LIM       0xE00u
#define LOWER_LIM       0x1FFu

// Joystick HW504:
// VRx -> ADC_CHANNEL_0 (ej: P0.23 AD0.0)
// VRy -> ADC_CHANNEL_1 (ej: P0.24 AD0.1)
// SW  -> P2.10 (entrada con pull-up)
#define JOY_BTN_PORT    2u
#define JOY_BTN_PIN     10u

// Botón extra para rotación de barco -> P2.11
#define ROT_BTN_PORT    2u
#define ROT_BTN_PIN     11u

// =================== Base de tiempo para la librería (TIMER1) ===================

// "milisegundos lógicos" para la lib (no tiene que ser exacto, solo monótono)
static volatile uint32_t g_animTimeMs = 0u;

// Prototipos HAL que usa battleship_max
uint32_t BS_Hal_GetMillis(void);
uint32_t BS_Hal_GetRandom(void);

// RNG simple
static uint32_t g_lcg_state = 1234567u;

// Implementación HAL: tiempo
uint32_t BS_Hal_GetMillis(void) {
    return g_animTimeMs;
}

// Implementación HAL: random
uint32_t BS_Hal_GetRandom(void) {
    g_lcg_state = 1664525u * g_lcg_state + 1013904223u;
    return g_lcg_state;
}

// TIMER1: genera ticks de animación para el blink
static void Timer1_Init_Anim(void) {
    TIM_TIMERCFG_Type cfgTimer1;
    TIM_MATCHCFG_Type cfgMatch10;

    cfgTimer1.prescaleOption = TIM_USVAL;
    cfgTimer1.prescaleValue  = 1000;   // 1 tick de TC = 1 ms

    TIM_Init(LPC_TIM1, TIM_TIMER_MODE, &cfgTimer1);

    // Match0 cada 100 ms
    cfgMatch10.matchChannel       = TIM_MATCH_0;
    cfgMatch10.intOnMatch         = ENABLE;
    cfgMatch10.resetOnMatch       = ENABLE;
    cfgMatch10.stopOnMatch        = DISABLE;
    cfgMatch10.extMatchOutputType = TIM_EXTMATCH_NOTHING;
    cfgMatch10.matchValue         = 99;   // 0..99 -> 100 ms

    TIM_ConfigMatch(LPC_TIM1, &cfgMatch10);
    TIM_Cmd(LPC_TIM1, ENABLE);

    NVIC_EnableIRQ(TIMER1_IRQn);
}

// Handler TIMER1: actualiza tiempo y llama a la animación de la lib
void TIMER1_IRQHandler(void) {
    g_animTimeMs += 100u;   // 100 ms por interrupción
    BS_AnimationsUpdate(g_animTimeMs);
    TIM_ClearIntPending(LPC_TIM1, TIM_MR0_INT);
}

// ================= Timer3: cuenta regresiva infinita en bloque 3 =================

static void Timer3_Init_1Hz(void) {
    TIM_TIMERCFG_Type cfgTimer3;
    TIM_MATCHCFG_Type cfgMatch03;

    cfgTimer3.prescaleOption = TIM_USVAL;
    cfgTimer3.prescaleValue  = 1000;   // 1 ms por tick
    TIM_Init(LPC_TIM3, TIM_TIMER_MODE, &cfgTimer3);

    // Match0 cada 1000 ms -> 1 segundo
    cfgMatch03.matchChannel       = TIM_MATCH_0;
    cfgMatch03.intOnMatch         = ENABLE;
    cfgMatch03.resetOnMatch       = ENABLE;
    cfgMatch03.stopOnMatch        = DISABLE;
    cfgMatch03.extMatchOutputType = TIM_EXTMATCH_NOTHING;
    cfgMatch03.matchValue         = 999;   // 0..999 -> 1000 ms

    TIM_ConfigMatch(LPC_TIM3, &cfgMatch03);
    TIM_Cmd(LPC_TIM3, ENABLE);

    NVIC_EnableIRQ(TIMER3_IRQn);
}

void TIMER3_IRQHandler(void) {
    uint8_t finished = BS_CountdownStep();
    if (finished) {
        // Cuando llega a 0, reseteamos a 9 para que sea infinito
        BS_CountdownSet(9u);
    }
    TIM_ClearIntPending(LPC_TIM3, TIM_MR0_INT);
}

// ================ Joystick / ADC ================

static volatile uint32_t vrx = 0u;
static volatile uint32_t vry = 0u;

// Estado previo de los botones para detectar flancos
static volatile uint8_t joyBtnLast = 1u;  // SW joystick
static volatile uint8_t rotBtnLast = 1u;  // botón rotación

// Flag: todos los barcos (2,4,6) ya colocados
static volatile uint8_t g_allShipsPlaced = 0u;

// Prototipos
static void cfgGPIO(void);
static void cfgTimerForADC(void);
static void cfgADC(void);
static void GPIO_Port2_UnusedAsOutput(void);

// ================ main ================

int main(void) {
    SystemInit();

    // Inicializar SPI0 + MAX + lógica completa Battleship
    MAX_SPI0_Init();
    BS_GameInit();
    // En este punto:
    //  - Bloque0: pivote/barco en preview
    //  - Bloque1: tablero jugador
    //  - Bloque2: contrincante con barcos 2,4,6
    //  - Bloque3: inicializado por la librería

    // Contador del bloque 3 arranca en 9 y se actualiza con Timer3
    BS_CountdownSet(9u);
    Timer3_Init_1Hz();

    // Timer1 para animaciones (blink/error/barcos listos/disparos agua)
    Timer1_Init_Anim();

    cfgGPIO();
    GPIO_Port2_UnusedAsOutput();
    cfgTimerForADC();
    cfgADC();

    while (1) {
        // Todo se maneja por interrupciones:
        //  - TIMER1_IRQHandler -> BS_AnimationsUpdate() (blink)
        //  - TIMER3_IRQHandler -> BS_CountdownStep() (contador)
        //  - ADC_IRQHandler    -> joystick + botones
        __WFI();   // opcional
    }
}

// ================ GPIO (joystick + rotación + limpieza PORT2) ================

static void cfgGPIO(void) {
    PINSEL_CFG_Type cfgPin;

    // --- Botón del joystick en P2.10 como GPIO entrada con pull-up ---
    cfgPin.portNum   = PINSEL_PORT_2;
    cfgPin.pinNum    = PINSEL_PIN_10;
    cfgPin.funcNum   = PINSEL_FUNC_0;      // GPIO
    cfgPin.pinMode   = PINSEL_PULLUP;
    cfgPin.openDrain = PINSEL_OD_NORMAL;
    PINSEL_ConfigPin(&cfgPin);

    GPIO_SetDir(JOY_BTN_PORT, BIT(JOY_BTN_PIN), 0); // 0 = input

    // --- Botón de rotación en P2.11 como GPIO entrada con pull-up ---
    cfgPin.pinNum    = PINSEL_PIN_11;
    cfgPin.funcNum   = PINSEL_FUNC_0;
    cfgPin.pinMode   = PINSEL_PULLUP;
    cfgPin.openDrain = PINSEL_OD_NORMAL;
    PINSEL_ConfigPin(&cfgPin);

    GPIO_SetDir(ROT_BTN_PORT, BIT(ROT_BTN_PIN), 0); // 0 = input
}

// Poner el resto de los pines de PORT2 como salida para que no floten
static void GPIO_Port2_UnusedAsOutput(void) {
    uint32_t mask_inputs = (1u << JOY_BTN_PIN) | (1u << ROT_BTN_PIN);
    LPC_GPIO2->FIODIR |= ~mask_inputs;
}

// ================ Timer0: trigger para ADC =================

static void cfgTimerForADC(void) {
    TIM_TIMERCFG_Type cfgTimer;
    TIM_MATCHCFG_Type cfgMatch01;

    // Timer base: prescale en us -> 1 tick = 1 ms
    cfgTimer.prescaleOption = TIM_USVAL;
    cfgTimer.prescaleValue  = 1000;
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &cfgTimer);

    // Match canal 1 (MAT0.1) para disparar el ADC cada ~125 ms
    cfgMatch01.matchChannel       = TIM_MATCH_1;
    cfgMatch01.intOnMatch         = DISABLE;
    cfgMatch01.resetOnMatch       = ENABLE;
    cfgMatch01.stopOnMatch        = DISABLE;
    cfgMatch01.extMatchOutputType = TIM_TOGGLE;
    cfgMatch01.matchValue         = 74;   // ~125 ms

    TIM_ConfigMatch(LPC_TIM0, &cfgMatch01);
    TIM_Cmd(LPC_TIM0, ENABLE);
}

// ================ ADC: joystick X/Y + botones =================

static void cfgADC(void) {
    // Inicializar ADC a tu rate
    ADC_Init(ADC_RATE);

    // Pines analógicos para CH0 (VRx) y CH1 (VRy)
    ADC_PinConfig(ADC_CHANNEL_0);
    ADC_PinConfig(ADC_CHANNEL_1);

    // Sin burst, disparo por MAT0.1
    ADC_BurstCmd(DISABLE);
    ADC_StartCmd(ADC_START_ON_MAT01);
    ADC_EdgeStartConfig(ADC_START_ON_FALLING);

    // Empieza leyendo CH0
    ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);
    ADC_ChannelCmd(ADC_CHANNEL_1, DISABLE);

    // Habilitamos interrupción en CH0 y CH1
    ADC_IntConfig(ADC_CHANNEL_0, ENABLE);
    ADC_IntConfig(ADC_CHANNEL_1, ENABLE);

    NVIC_EnableIRQ(ADC_IRQn);
}

void ADC_IRQHandler(void) {
    // 1) Lectura del joystick analógico (VRx / VRy)
    if (ADC_GlobalGetStatus(ADC_DATA_DONE)) {
        if (ADC_ChannelGetStatus(ADC_CHANNEL_0, ADC_DATA_DONE)) {
            // Cambiar a CH1 para la próxima conversión
            ADC_ChannelCmd(ADC_CHANNEL_0, DISABLE);
            ADC_ChannelCmd(ADC_CHANNEL_1, ENABLE);

            vrx = ADC_ChannelGetData(ADC_CHANNEL_0);

            // Movimiento horizontal con VRx
            BS_Mode mode = BS_GetMode();
            if (vrx > UPPER_LIM) {
                // derecha
                if (mode == MODE_PLACE) {
                    BS_Placement_MoveCursor(BS_DIR_RIGHT);
                } else { // MODE_SHOT
                    BS_Shot_MoveCursor(BS_DIR_RIGHT);
                }
            } else if (vrx < LOWER_LIM) {
                // izquierda
                if (mode == MODE_PLACE) {
                    BS_Placement_MoveCursor(BS_DIR_LEFT);
                } else {
                    BS_Shot_MoveCursor(BS_DIR_LEFT);
                }
            }

        } else if (ADC_ChannelGetStatus(ADC_CHANNEL_1, ADC_DATA_DONE)) {
            // Cambiar de nuevo a CH0
            ADC_ChannelCmd(ADC_CHANNEL_1, DISABLE);
            ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);

            vry = ADC_ChannelGetData(ADC_CHANNEL_1);

            // Movimiento vertical con VRy
            BS_Mode mode = BS_GetMode();
            if (vry > UPPER_LIM) {
                // arriba
                if (mode == MODE_PLACE) {
                    BS_Placement_MoveCursor(BS_DIR_UP);
                } else {
                    BS_Shot_MoveCursor(BS_DIR_UP);
                }
            } else if (vry < LOWER_LIM) {
                // abajo
                if (mode == MODE_PLACE) {
                    BS_Placement_MoveCursor(BS_DIR_DOWN);
                } else {
                    BS_Shot_MoveCursor(BS_DIR_DOWN);
                }
            }
        }
    }

    // 2) Botón del joystick (SW en P2.10): colocar / confirmar / disparar
    {
        uint8_t joyNow = (GPIO_ReadValue(JOY_BTN_PORT) & BIT(JOY_BTN_PIN)) ? 1u : 0u;

        if (joyNow == 0u && joyBtnLast == 1u) { // flanco de bajada
            BS_Mode mode = BS_GetMode();

            if (mode == MODE_PLACE) {
                if (!g_allShipsPlaced) {
                    // Intentar colocar barco actual (2,4,6)
                    BS_PlaceResult res = BS_Placement_TryPlaceCurrentShip(BS_Hal_GetMillis());
                    if (res == BS_PLACE_ALL_DONE) {
                        g_allShipsPlaced = 1u;
                        // La librería puede activar un blink especial hasta confirmar
                    }
                    // Si res == BS_PLACE_INVALID, la librería activa blink de error
                } else {
                    // Todos los barcos ya colocados, y seguimos en MODE_PLACE:
                    // este mismo botón actúa como "confirmar" -> pasamos a modo disparo
                    BS_EnterShotMode();
                }
            } else { // MODE_SHOT
                // En modo disparo: este botón dispara al contrincante
                SHOT_RESULT_t sres = BS_Shot_FireAtCursor();
                (void)sres;
                // La librería decide blink por agua (MISS) o dejar HIT fijo
            }
        }

        joyBtnLast = joyNow;
    }

    // 3) Botón de rotación (P2.11): solo durante colocación y antes de terminar
    {
        uint8_t rotNow = (GPIO_ReadValue(ROT_BTN_PORT) & BIT(ROT_BTN_PIN)) ? 1u : 0u;

        if (rotNow == 0u && rotBtnLast == 1u) { // flanco de bajada
            if (BS_GetMode() == MODE_PLACE && !g_allShipsPlaced) {
                BS_Placement_RotateCursor();
            }
        }

        rotBtnLast = rotNow;
    }
}
