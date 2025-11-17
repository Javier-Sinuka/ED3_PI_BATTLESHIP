#include "LPC17xx.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_adc.h"

#include "max_controll.h"
#include "battleship_max.h"

// ======================================================
//  DEFINES
// ======================================================
#define BIT(x)            (1u << (x))

#define ADC_RATE          180000u
#define UPPER_LIM         0xE00u
#define LOWER_LIM         0x1FFu

// Joystick HW504
// VRx -> ADC_CHANNEL_0
// VRy -> ADC_CHANNEL_1
// SW  -> P2.10 (GPIO entrada con pull-up)
#define JOY_BTN_PORT      2u
#define JOY_BTN_PIN       10u

static volatile uint32_t vrx = 0;
static volatile uint32_t vry = 0;
static volatile uint32_t fakeTime = 0;
static volatile uint8_t  joyBtnLast = 1;    // pull-up → 1 = no presionado

// ======================================================
//  PROTOTIPOS
// ======================================================
static void cfgGPIO(void);
static void cfgTimerForADC(void);
static void cfgADC(void);

// Blink sucio por software (solo delay)
static void BlinkErrorBusy(void);

// ======================================================
//  MAIN
// ======================================================

int main(void)
{
    SystemInit();

    // Init MAX7219 + Battleship Logic
    MAX_SPI0_Init();
    BS_GameInit();   // bloque0 pivote/barcos, bloque1 jugador, bloque2 contrincante, etc.

    cfgGPIO();
    cfgTimerForADC();   // Timer0 con matchValue = 124
    cfgADC();

    while(1)
    {
        __WFI();   // Todo se maneja por IRQ
    }

    // Nunca llega acá
    // return 0;
}

// ======================================================
//  SOFTWARE BLINK (solo delay bloqueante)
// ======================================================
static void BlinkErrorBusy(void)
{
    // 4 "parpadeos" ficticios (solo tiempo, no modificamos el display)
    for (int i = 0; i < 4; i++)
    {
        for (volatile int d = 0; d < 180000; d++) {
            __NOP();
        }
    }
}

// ======================================================
//  GPIO – Botón del Joystick
// ======================================================

static void cfgGPIO(void)
{
    PINSEL_CFG_Type cfg;

    // P2.10 como GPIO con pull-up (botón SW del joystick)
    cfg.portNum   = PINSEL_PORT_2;
    cfg.pinNum    = PINSEL_PIN_10;
    cfg.funcNum   = PINSEL_FUNC_0;        // GPIO
    cfg.pinMode   = PINSEL_PULLUP;
    cfg.openDrain = PINSEL_OD_NORMAL;
    PINSEL_ConfigPin(&cfg);

    GPIO_SetDir(JOY_BTN_PORT, BIT(JOY_BTN_PIN), 0);  // entrada
}

// ======================================================
//  TIMER0 – Trigger para ADC (match en 124)
// ======================================================

static void cfgTimerForADC(void)
{
    TIM_TIMERCFG_Type cfgT;
    cfgT.prescaleOption = TIM_USVAL;
    cfgT.prescaleValue  = 1000;     // 1 ms cada tick

    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &cfgT);

    TIM_MATCHCFG_Type m1;
    m1.matchChannel       = TIM_MATCH_1;
    m1.intOnMatch         = DISABLE;
    m1.resetOnMatch       = ENABLE;
    m1.stopOnMatch        = DISABLE;
    m1.extMatchOutputType = TIM_TOGGLE;
    m1.matchValue         = 124;     // valor original

    TIM_ConfigMatch(LPC_TIM0, &m1);
    TIM_Cmd(LPC_TIM0, ENABLE);
}

// ======================================================
//  ADC – Joystick X/Y + botón (leer botón en la misma IRQ)
// ======================================================

static void cfgADC(void)
{
    ADC_Init(ADC_RATE);

    // VRx y VRy
    ADC_PinConfig(ADC_CHANNEL_0);   // VRx
    ADC_PinConfig(ADC_CHANNEL_1);   // VRy

    ADC_BurstCmd(DISABLE);
    ADC_StartCmd(ADC_START_ON_MAT01);
    ADC_EdgeStartConfig(ADC_START_ON_FALLING);

    // Arrancamos leyendo CH0
    ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);
    ADC_ChannelCmd(ADC_CHANNEL_1, DISABLE);

    ADC_IntConfig(ADC_CHANNEL_0, ENABLE);
    ADC_IntConfig(ADC_CHANNEL_1, ENABLE);

    NVIC_EnableIRQ(ADC_IRQn);
}

void ADC_IRQHandler(void)
{
    fakeTime += 8;   // reloj lógico para BS_Placement_TryPlaceCurrentShip()

    if (!ADC_GlobalGetStatus(ADC_DATA_DONE))
        return;

    // ==================================================
    //  CANAL X (VRx)
    // ==================================================
    if (ADC_ChannelGetStatus(ADC_CHANNEL_0, ADC_DATA_DONE))
    {
        ADC_ChannelCmd(ADC_CHANNEL_0, DISABLE);
        ADC_ChannelCmd(ADC_CHANNEL_1, ENABLE);

        vrx = ADC_ChannelGetData(ADC_CHANNEL_0);

        if (vrx > UPPER_LIM) {
            BS_Placement_MoveCursor(BS_DIR_RIGHT);
        }
        else if (vrx < LOWER_LIM) {
            BS_Placement_MoveCursor(BS_DIR_LEFT);
        }
    }

    // ==================================================
    //  CANAL Y (VRy)
    // ==================================================
    else if (ADC_ChannelGetStatus(ADC_CHANNEL_1, ADC_DATA_DONE))
    {
        ADC_ChannelCmd(ADC_CHANNEL_1, DISABLE);
        ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);

        vry = ADC_ChannelGetData(ADC_CHANNEL_1);

        if (vry > UPPER_LIM) {
            BS_Placement_MoveCursor(BS_DIR_UP);
        }
        else if (vry < LOWER_LIM) {
            BS_Placement_MoveCursor(BS_DIR_DOWN);
        }
    }

    // ==================================================
    //  BOTÓN DEL JOYSTICK → colocar barco
    // ==================================================
    {
        uint8_t now = (GPIO_ReadValue(JOY_BTN_PORT) & BIT(JOY_BTN_PIN)) ? 1u : 0u;

        if (now == 0u && joyBtnLast == 1u)   // flanco de bajada
        {
            BS_PlaceResult r = BS_Placement_TryPlaceCurrentShip(fakeTime);

            // Usamos solo enums que seguro existen:
            //  - BS_PLACE_INVALID
            //  - BS_PLACE_ALL_DONE
            if (r == BS_PLACE_INVALID)
            {
                // Blink sucio cuando no se puede colocar
                BlinkErrorBusy();
            }
            else if (r == BS_PLACE_ALL_DONE)
            {
                // Opcional: indicar que se terminaron de colocar los barcos
                BlinkErrorBusy();
            }
        }

        joyBtnLast = now;
    }
}
