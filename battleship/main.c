// main.c
#include "LPC17xx.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_adc.h"

#include "max_controll.h"
#include "battleship_max.h"

// ================= Macros base =================

#define BIT(x)          (1u << (x))

// Ratio del ADC
#define ADC_RATE        180000u

// Límites de movimiento del joystick
#define UPPER_LIM       0xE00u
#define LOWER_LIM       0x1FFu

// Joystick HW504:
// VRx -> ADC_CHANNEL_0
// VRy -> ADC_CHANNEL_1
// SW  -> P2.10 (entrada con pull-up)
#define JOY_BTN_PORT    2u
#define JOY_BTN_PIN     10u

// Botón extra para rotación de barco -> P2.11
#define ROT_BTN_PORT    2u
#define ROT_BTN_PIN     11u

// ================ Base de tiempo (HAL para la librería) =================

static volatile uint32_t g_msTicks = 0u;
static uint32_t lcg_state = 1234567u;

// Prototipos HAL que usa la librería
uint32_t BS_Hal_GetMillis(void);
uint32_t BS_Hal_GetRandom(void);

// SysTick a 1 ms REAL
static void configSysTick(void) {
    // Asegurarse de que SystemCoreClock tiene el valor correcto
    SystemCoreClockUpdate();

    uint32_t reload = SystemCoreClock / 1000u;  // 1 ms

    SysTick->LOAD = reload - 1u;
    SysTick->VAL  = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk   |
                    SysTick_CTRL_ENABLE_Msk;
}

// SysTick: incrementa ms y deja que la librería maneje blinks, etc.
void SysTick_Handler(void) {
    g_msTicks++;
    BS_AnimationsUpdate(g_msTicks);
}

uint32_t BS_Hal_GetMillis(void) {
    return g_msTicks;
}

uint32_t BS_Hal_GetRandom(void) {
    // LCG simple
    lcg_state = 1664525u * lcg_state + 1013904223u;
    return lcg_state;
}

// ================= Timer3: cuenta regresiva infinita en bloque 3 =================

// Timer3 a ~1 Hz para llamar BS_CountdownStep()
static void Timer3_Init_1Hz(void) {
    TIM_TIMERCFG_Type cfgTimer3;
    TIM_MATCHCFG_Type cfgMatch03;

    // Timer base en us, prescale a 1000 -> tick = 1 ms
    cfgTimer3.prescaleOption = TIM_USVAL;
    cfgTimer3.prescaleValue  = 1000;
    TIM_Init(LPC_TIM3, TIM_TIMER_MODE, &cfgTimer3);

    // Match canal 0 (MR0) para 1 segundo aprox: 1000 ticks de 1 ms
    cfgMatch03.matchChannel       = TIM_MATCH_0;
    cfgMatch03.intOnMatch         = ENABLE;
    cfgMatch03.stopOnMatch        = DISABLE;
    cfgMatch03.resetOnMatch       = ENABLE;
    cfgMatch03.extMatchOutputType = TIM_TOGGLE;  // no nos importa la salida
    cfgMatch03.matchValue         = 999;         // 0..999 -> 1000 ms

    TIM_ConfigMatch(LPC_TIM3, &cfgMatch03);
    TIM_Cmd(LPC_TIM3, ENABLE);

    NVIC_EnableIRQ(TIMER3_IRQn);
}

void TIMER3_IRQHandler(void) {
    // Cada 1 s aproximado, avanzamos la cuenta regresiva del bloque 3
    uint8_t finished = BS_CountdownStep();
    if (finished) {
        // Cuando llega a 0, lo reseteamos a 9 para que sea infinito
        BS_CountdownSet(9u);
    }
    TIM_ClearIntPending(LPC_TIM3, TIM_MR0_INT);
}

// ================ Joystick / ADC ================

static volatile uint32_t vrx = 0u;
static volatile uint32_t vry = 0u;

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

    // SysTick para base de tiempo de la librería (blink, etc.)
    configSysTick();

    // Inicializar lógica completa Battleship + MAX internamente
    BS_GameInit();

    // Contador del bloque 3 arranca en 9 y se actualiza con Timer3
    BS_CountdownSet(9u);
    Timer3_Init_1Hz();

    cfgGPIO();
    GPIO_Port2_UnusedAsOutput();
    cfgTimerForADC();
    cfgADC();

    while (1) {
        // Todo se maneja por interrupciones:
        //  - SysTick_Handler -> BS_AnimationsUpdate()
        //  - TIMER3_IRQHandler -> BS_CountdownStep()
        //  - ADC_IRQHandler -> joystick analógico
        //  - EINT3_IRQHandler -> botones (colocar / rotar / disparar)
        __WFI();   // opcional: dormir hasta próxima IRQ
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

    // --- GPIO interrupt en flanco descendente para P2.10 y P2.11 ---
    // Limpiamos cualquier pendiente previa
    LPC_GPIOINT->IO2IntClr = (1u << JOY_BTN_PIN) | (1u << ROT_BTN_PIN);
    // Habilitamos interrupción por flanco de bajada
    LPC_GPIOINT->IO2IntEnF |= (1u << JOY_BTN_PIN) | (1u << ROT_BTN_PIN);

    NVIC_EnableIRQ(EINT3_IRQn);
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

    // Timer base: prescale en us -> 1 tick = 1000 us = 1 ms
    cfgTimer.prescaleOption = TIM_USVAL;
    cfgTimer.prescaleValue  = 1000;
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &cfgTimer);

    // Match canal 1 (MAT0.1) para disparar el ADC cada ~125 ms
    cfgMatch01.matchChannel       = TIM_MATCH_1;
    cfgMatch01.intOnMatch         = DISABLE;
    cfgMatch01.resetOnMatch       = ENABLE;
    cfgMatch01.stopOnMatch        = DISABLE;
    cfgMatch01.extMatchOutputType = TIM_TOGGLE;
    cfgMatch01.matchValue         = 124;   // 0..124 -> 125 ms aprox.

    TIM_ConfigMatch(LPC_TIM0, &cfgMatch01);
    TIM_Cmd(LPC_TIM0, ENABLE);
}

// ================ ADC: joystick X/Y =================

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

    // battleship_max se encarga internamente de:
    //  - refrescar bloques 0,1,2 en cada acción
    //  - blinks de error, barcos listos, agua, etc. vía BS_AnimationsUpdate()
}

// ================ GPIO IRQ: botones (colocar / rotar / disparar) ================

void EINT3_IRQHandler(void) {
    uint32_t statusF = LPC_GPIOINT->IO2IntStatF;

    // --- Botón del joystick (P2.10) -> colocar/confirmar/disparar ---
    if (statusF & (1u << JOY_BTN_PIN)) {
        // limpiar primero el flag de interrupción
        LPC_GPIOINT->IO2IntClr = (1u << JOY_BTN_PIN);

        BS_Mode mode = BS_GetMode();

        if (mode == MODE_PLACE) {
            if (!g_allShipsPlaced) {
                // Intentar colocar barco actual (2,4,6)
                BS_PlaceResult res = BS_Placement_TryPlaceCurrentShip(BS_Hal_GetMillis());
                if (res == BS_PLACE_ALL_DONE) {
                    // Ya colocamos los 3 barcos
                    g_allShipsPlaced = 1u;
                }
                // Si res == BS_PLACE_INVALID, la lib maneja el blink de error
            } else {
                // Todos los barcos ya colocados, este botón pasa a modo disparo
                BS_EnterShotMode();
            }
        } else { // MODE_SHOT
            // En modo disparo: este botón dispara al contrincante
            SHOT_RESULT_t sres = BS_Shot_FireAtCursor();
            (void)sres;
            // La librería se encarga de:
            //  - HIT: mantener led encendido (queda fijo)
            //  - MISS: blink en el agua y luego se apaga (por animación)
        }
    }

    // --- Botón de rotación (P2.11) -> solo durante colocación y antes de terminar ---
    if (statusF & (1u << ROT_BTN_PIN)) {
        LPC_GPIOINT->IO2IntClr = (1u << ROT_BTN_PIN);

        if (BS_GetMode() == MODE_PLACE && !g_allShipsPlaced) {
            BS_Placement_RotateCursor();
        }
    }
}