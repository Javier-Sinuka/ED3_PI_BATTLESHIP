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

// =================== Timer1: base de animación (blink) ===================

// NO definimos BS_Hal_GetMillis ni BS_Hal_GetRandom acá,
// ya están en hw_time.c

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
    cfgMatch10.extMatchOutputType = TIM_NOTHING;
    cfgMatch10.matchValue         = 99;   // 0..99 -> 100 ms

    TIM_ConfigMatch(LPC_TIM1, &cfgMatch10);
    TIM_Cmd(LPC_TIM1, ENABLE);

    NVIC_EnableIRQ(TIMER1_IRQn);
}

// Handler TIMER1: llama a la animación de la lib usando el tiempo de hw_time.c
void TIMER1_IRQHandler(void) {
    BS_AnimationsUpdate(BS_Hal_GetMillis());
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
    cfgMatch03.extMatchOutputType = TIM_NOTHING;
    cfgMatch03.matchValue         = 999;   // 0..999 -> 1000 ms

    TIM_ConfigMatch(LPC_TIM3, &cfgMatch03);
    TIM_Cmd(LPC_TIM3, ENABLE);

    NVIC_EnableIRQ(TIMER3_IRQn);
}

void TIMER3_IRQHandler(void) {
    uint8_t finished = BS_CountdownStep();
    if (finished) {
        BS_CountdownSet(9u);
    }
    TIM_ClearIntPending(LPC_TIM3, TIM_MR0_INT);
}

// ================ Joystick / ADC ================

static volatile uint32_t vrx = 0u;
static volatile uint32_t vry = 0u;

static volatile uint8_t joyBtnLast = 1u;  // SW joystick
static volatile uint8_t rotBtnLast = 1u;  // botón rotación
static volatile uint8_t g_allShipsPlaced = 0u;

// Prototipos
static void cfgGPIO(void);
static void cfgTimerForADC(void);
static void cfgADC(void);
static void GPIO_Port2_UnusedAsOutput(void);

// ================ main ================

int main(void) {
    SystemInit();

    MAX_SPI0_Init();
    BS_GameInit();

    BS_CountdownSet(9u);
    Timer3_Init_1Hz();      // contador bloque3
    Timer1_Init_Anim();     // blink (usa BS_Hal_GetMillis de hw_time.c)

    cfgGPIO();
    GPIO_Port2_UnusedAsOutput();
    cfgTimerForADC();
    cfgADC();

    while (1) {
        __WFI();
    }
}

// ================ GPIO (joystick + rotación + limpieza PORT2) ================

static void cfgGPIO(void) {
    PINSEL_CFG_Type cfgPin;

    // Botón del joystick en P2.10
    cfgPin.portNum   = PINSEL_PORT_2;
    cfgPin.pinNum    = PINSEL_PIN_10;
    cfgPin.funcNum   = PINSEL_FUNC_0;
    cfgPin.pinMode   = PINSEL_PULLUP;
    cfgPin.openDrain = PINSEL_OD_NORMAL;
    PINSEL_ConfigPin(&cfgPin);
    GPIO_SetDir(JOY_BTN_PORT, BIT(JOY_BTN_PIN), 0);

    // Botón de rotación en P2.11
    cfgPin.pinNum    = PINSEL_PIN_11;
    cfgPin.funcNum   = PINSEL_FUNC_0;
    cfgPin.pinMode   = PINSEL_PULLUP;
    cfgPin.openDrain = PINSEL_OD_NORMAL;
    PINSEL_ConfigPin(&cfgPin);
    GPIO_SetDir(ROT_BTN_PORT, BIT(ROT_BTN_PIN), 0);
}

static void GPIO_Port2_UnusedAsOutput(void) {
    uint32_t mask_inputs = (1u << JOY_BTN_PIN) | (1u << ROT_BTN_PIN);
    LPC_GPIO2->FIODIR |= ~mask_inputs;
}

// ================ Timer0: trigger para ADC =================

static void cfgTimerForADC(void) {
    TIM_TIMERCFG_Type cfgTimer;
    TIM_MATCHCFG_Type cfgMatch01;

    cfgTimer.prescaleOption = TIM_USVAL;
    cfgTimer.prescaleValue  = 1000;
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &cfgTimer);

    cfgMatch01.matchChannel       = TIM_MATCH_1;
    cfgMatch01.intOnMatch         = DISABLE;
    cfgMatch01.resetOnMatch       = ENABLE;
    cfgMatch01.stopOnMatch        = DISABLE;
    cfgMatch01.extMatchOutputType = TIM_TOGGLE;
    cfgMatch01.matchValue         = 124;   // ~125 ms

    TIM_ConfigMatch(LPC_TIM0, &cfgMatch01);
    TIM_Cmd(LPC_TIM0, ENABLE);
}

// ================ ADC: joystick X/Y + botones =================

static void cfgADC(void) {
    ADC_Init(ADC_RATE);

    ADC_PinConfig(ADC_CHANNEL_0);
    ADC_PinConfig(ADC_CHANNEL_1);

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

    // Botón joystick: colocar/confirmar/disparar
    {
        uint8_t joyNow = (GPIO_ReadValue(JOY_BTN_PORT) & BIT(JOY_BTN_PIN)) ? 1u : 0u;

        if (joyNow == 0u && joyBtnLast == 1u) {
            BS_Mode mode = BS_GetMode();

            if (mode == MODE_PLACE) {
                if (!g_allShipsPlaced) {
                    BS_PlaceResult res = BS_Placement_TryPlaceCurrentShip(BS_Hal_GetMillis());
                    if (res == BS_PLACE_ALL_DONE) {
                        g_allShipsPlaced = 1u;
                    }
                } else {
                    BS_EnterShotMode();
                }
            } else { // MODE_SHOT
                SHOT_RESULT_t sres = BS_Shot_FireAtCursor();
                (void)sres;
            }
        }
        joyBtnLast = joyNow;
    }

    // Botón rotación
    {
        uint8_t rotNow = (GPIO_ReadValue(ROT_BTN_PORT) & BIT(ROT_BTN_PIN)) ? 1u : 0u;

        if (rotNow == 0u && rotBtnLast == 1u) {
            if (BS_GetMode() == MODE_PLACE && !g_allShipsPlaced) {
                BS_Placement_RotateCursor();
            }
        }
        rotBtnLast = rotNow;
    }
}
