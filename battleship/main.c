#include "LPC17xx.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_exti.h"

#include "max_controll.h"
#include "battleship_max.h"

#define BIT(x)          (1u << (x))
#define ADC_RATE        180000u
#define UPPER_LIM       0xE00u
#define LOWER_LIM       0x1FFu

// Joystick HW504
#define JOY_BTN_PORT    2u   // P2.10 -> EINT0
#define JOY_BTN_PIN     10u

// Botón rotación -> EINT1 en P2.11
#define ROT_BTN_PORT    2u
#define ROT_BTN_PIN     11u

static volatile uint32_t vrx = 0u;
static volatile uint32_t vry = 0u;
static volatile uint8_t  g_allShipsPlaced = 0u;

// ========== Delay “sucio” ==========
static void delayMs_busy(uint32_t ms) {
    volatile uint32_t i;
    while (ms--) {
        for (i = 0; i < 10000u; i++) {
            __NOP();
        }
    }
}

// Blink de error “sucio” (jugador) ─ intensidad del bloque 1
static void BlinkErrorBusy(void) {
    uint8_t i;
    for (i = 0; i < 4u; i++) {
        MAX_SetIntensity(BS_DEV_PLAYER, 0);   // casi apagado
        delayMs_busy(80);
        MAX_SetIntensity(BS_DEV_PLAYER, 8);   // intensidad media
        delayMs_busy(80);
    }
}

// ========= Prototipos =========
static void cfgGPIO(void);
static void cfgEINT(void);
static void cfgTimerForADC(void);
static void cfgADC(void);
static void GPIO_Port2_UnusedAsOutput(void);

// ================ main ================
int main(void) {
    SystemInit();

    MAX_SPI0_Init();
    BS_GameInit();

    // si seguís usando el contador del bloque3:
    BS_CountdownSet(9u);
    // Timer3_Init_1Hz();  // si lo querés dejar, mantenelo como antes

    cfgGPIO();
    GPIO_Port2_UnusedAsOutput();
    cfgEINT();
    cfgTimerForADC();
    cfgADC();

    while (1) {
        __WFI();
    }
}

// ========== GPIO: EINT0 / EINT1 + limpieza PORT2 ==========
static void cfgGPIO(void) {
    PINSEL_CFG_Type cfgPin;

    // P2.10 como EINT0 (función 1), con pull-up
    cfgPin.portNum   = PINSEL_PORT_2;
    cfgPin.pinNum    = PINSEL_PIN_10;
    cfgPin.funcNum   = PINSEL_FUNC_1;         // EINT0
    cfgPin.pinMode   = PINSEL_PULLUP;
    cfgPin.openDrain = PINSEL_OD_NORMAL;
    PINSEL_ConfigPin(&cfgPin);

    // P2.11 como EINT1 (función 1), con pull-up
    cfgPin.pinNum    = PINSEL_PIN_11;
    cfgPin.funcNum   = PINSEL_FUNC_1;         // EINT1
    cfgPin.pinMode   = PINSEL_PULLUP;
    cfgPin.openDrain = PINSEL_OD_NORMAL;
    PINSEL_ConfigPin(&cfgPin);

    // Opcional: igual los tratamos como entradas para GPIO también
    GPIO_SetDir(JOY_BTN_PORT, BIT(JOY_BTN_PIN), 0);
    GPIO_SetDir(ROT_BTN_PORT, BIT(ROT_BTN_PIN), 0);
}

static void GPIO_Port2_UnusedAsOutput(void) {
    uint32_t mask_inputs = (1u << JOY_BTN_PIN) | (1u << ROT_BTN_PIN);
    LPC_GPIO2->FIODIR |= ~mask_inputs;
}

// ========== EXTI (EINT0 / EINT1) ==========
static void cfgEINT(void) {
    EXTI_Init();

    // Config pin para EINT0 y EINT1 con pull-up interno
    EXTI_PinConfig(EXTI_EINT0, EXTI_PULLUP);
    EXTI_PinConfig(EXTI_EINT1, EXTI_PULLUP);

    EXTI_CFG_Type cfgEXTI;
    cfgEXTI.mode     = EXTI_EDGE_SENSITIVE;
    cfgEXTI.polarity = EXTI_FALLING_EDGE;

    // EINT0: joystick SW -> colocar / confirmar / disparar
    cfgEXTI.line = EXTI_EINT0;
    EXTI_Config(&cfgEXTI);
    EXTI_ClearEXTIFlag(EXTI_EINT0);
    EXTI_Enable(EXTI_EINT0);      // habilita línea
    NVIC_EnableIRQ(EINT0_IRQn);   // habilita IRQ en NVIC

    // EINT1: botón rotación
    cfgEXTI.line = EXTI_EINT1;
    EXTI_Config(&cfgEXTI);
    EXTI_ClearEXTIFlag(EXTI_EINT1);
    EXTI_Enable(EXTI_EINT1);
    NVIC_EnableIRQ(EINT1_IRQn);
}

// ========== Timer0: trigger para ADC (con matchValue 74) ==========
static void cfgTimerForADC(void) {
    TIM_TIMERCFG_Type cfgTimer;
    TIM_MATCHCFG_Type cfgMatch01;

    cfgTimer.prescaleOption = TIM_USVAL;
    cfgTimer.prescaleValue  = 1000;     // 1 ms por tick
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &cfgTimer);

    cfgMatch01.matchChannel       = TIM_MATCH_1;
    cfgMatch01.intOnMatch         = DISABLE;
    cfgMatch01.resetOnMatch       = ENABLE;
    cfgMatch01.stopOnMatch        = DISABLE;
    cfgMatch01.extMatchOutputType = TIM_TOGGLE;
    cfgMatch01.matchValue         = 74;       // tu cambio: 75 ms aprox

    TIM_ConfigMatch(LPC_TIM0, &cfgMatch01);
    TIM_Cmd(LPC_TIM0, ENABLE);
}

// ========== ADC: joystick X/Y ==========
static void cfgADC(void) {
    ADC_Init(ADC_RATE);
    ADC_PinConfig(ADC_CHANNEL_0);   // VRx
    ADC_PinConfig(ADC_CHANNEL_1);   // VRy

    ADC_BurstCmd(DISABLE);
    ADC_StartCmd(ADC_START_ON_MAT01);
    ADC_EdgeStartConfig(ADC_START_ON_FALLING);

    ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);
    ADC_ChannelCmd(ADC_CHANNEL_1, DISABLE);

    ADC_IntConfig(ADC_CHANNEL_0, ENABLE);
    ADC_IntConfig(ADC_CHANNEL_1, ENABLE);

    NVIC_EnableIRQ(ADC_IRQn);
}

void ADC_IRQHandler(void) {
    if (ADC_GlobalGetStatus(ADC_DATA_DONE)) {
        if (ADC_ChannelGetStatus(ADC_CHANNEL_0, ADC_DATA_DONE)) {
            ADC_ChannelCmd(ADC_CHANNEL_0, DISABLE);
            ADC_ChannelCmd(ADC_CHANNEL_1, ENABLE);

            vrx = ADC_ChannelGetData(ADC_CHANNEL_0);

            BS_Mode mode = BS_GetMode();
            if (vrx > UPPER_LIM) {
                if (mode == MODE_PLACE) BS_Placement_MoveCursor(BS_DIR_RIGHT);
                else                    BS_Shot_MoveCursor(BS_DIR_RIGHT);
            } else if (vrx < LOWER_LIM) {
                if (mode == MODE_PLACE) BS_Placement_MoveCursor(BS_DIR_LEFT);
                else                    BS_Shot_MoveCursor(BS_DIR_LEFT);
            }

        } else if (ADC_ChannelGetStatus(ADC_CHANNEL_1, ADC_DATA_DONE)) {
            ADC_ChannelCmd(ADC_CHANNEL_1, DISABLE);
            ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);

            vry = ADC_ChannelGetData(ADC_CHANNEL_1);

            BS_Mode mode = BS_GetMode();
            if (vry > UPPER_LIM) {
                if (mode == MODE_PLACE) BS_Placement_MoveCursor(BS_DIR_UP);
                else                    BS_Shot_MoveCursor(BS_DIR_UP);
            } else if (vry < LOWER_LIM) {
                if (mode == MODE_PLACE) BS_Placement_MoveCursor(BS_DIR_DOWN);
                else                    BS_Shot_MoveCursor(BS_DIR_DOWN);
            }
        }
    }
}

// ========== EINT0: SW joystick (colocar / confirmar / disparar) ==========
void EINT0_IRQHandler(void) {
    BS_Mode mode = BS_GetMode();

    if (mode == MODE_PLACE) {
        if (!g_allShipsPlaced) {
            BS_PlaceResult res = BS_Placement_TryPlaceCurrentShip(0); // ahora el tiempo no importa
            if (res == BS_PLACE_INVALID) {
                // Blink "sucio" al estilo Arduino
                BlinkErrorBusy();
            } else if (res == BS_PLACE_ALL_DONE) {
                g_allShipsPlaced = 1u;
                // Podrías hacer otro blink distinto aquí si querés
            }
        } else {
            // Ya coloqué todos los barcos y sigo en MODE_PLACE:
            // este botón ahora actúa como "confirmar" -> entrar a modo disparo
            BS_EnterShotMode();
        }
    } else { // MODE_SHOT
        SHOT_RESULT_t sres = BS_Shot_FireAtCursor();
        if (sres == SHOT_MISS) {
            // Podés reusar BlinkErrorBusy o hacer otro patrón
            BlinkErrorBusy();
        }
    }

    EXTI_ClearEXTIFlag(EXTI_EINT0);
}

// ========== EINT1: botón de rotación ==========
void EINT1_IRQHandler(void) {
    if (BS_GetMode() == MODE_PLACE && !g_allShipsPlaced) {
        BS_Placement_RotateCursor();
    }
    EXTI_ClearEXTIFlag(EXTI_EINT1);
}
