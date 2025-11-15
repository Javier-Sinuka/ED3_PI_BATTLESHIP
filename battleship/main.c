#include "LPC17xx.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_adc.h"

#include "max_controll.h"
#include "battleship_max.h"

// ==== Macros de tu proyecto original ====
#define BIT(x)          (1u << (x))
// Ratio del ADC.
#define ADC_RATE        180000u
// Limites para definir un movimiento del analógico
#define UPPER_LIM       0xE00u
#define LOWER_LIM       0x1FFu

// ==== Joystick analógico HW504 ====
// Supongamos:
//  - VRx -> ADC_CHANNEL_0
//  - VRy -> ADC_CHANNEL_1
//  - SW  -> P2.10 (entrada, pull-up)
#define JOY_BTN_PORT    2u
#define JOY_BTN_PIN     10u

// ==== Variables globales para ADC ====
static volatile uint32_t vrx = 0u;
static volatile uint32_t vry = 0u;

// "Tiempo falso" para pasar a BS_Placement_TryPlaceCurrentShip()
static volatile uint32_t g_fakeTime = 0u;

// Estado previo del botón para detectar flanco
static volatile uint8_t joyBtnLast = 1u;  // 1 = no presionado (pull-up)

// ==== Prototipos ====
static void cfgGPIO(void);
static void cfgTimerForADC(void);
static void cfgADC(void);

// ======================== main ========================

int main(void) {
    SystemInit();

    // Inicializar SPI0 y MAX + lógica del Battleship
    MAX_SPI0_Init();
    BS_GameInit();
    // En este punto:
    //  - Bloque0: pivote/barco actual en preview
    //  - Bloque1: tablero jugador vacío
    //  - Bloque2: contrincante con barcos 2,4,6 (random)

    cfgGPIO();
    cfgTimerForADC();
    cfgADC();

    while (1) {
        // Todo se maneja por interrupciones de ADC (y Timer0 para trigger)
        __WFI();   // opcional: esperar interrupciones
    }

    // Nunca llega acá
    // return 0;
}

// =================== Configuración GPIO ===================

static void cfgGPIO(void) {
    PINSEL_CFG_Type cfgPin;

    // --- Botón del joystick en P2.10 como GPIO entrada con pull-up ---
    cfgPin.Portnum   = PINSEL_PORT_2;
    cfgPin.Pinnum    = PINSEL_PIN_10;
    cfgPin.Funcnum   = PINSEL_FUNC_0;      // GPIO
    cfgPin.Pinmode   = PINSEL_PINMODE_PULLUP;
    cfgPin.OpenDrain = PINSEL_PINMODE_NORMAL;
    PINSEL_ConfigPin(&cfgPin);

    // Dirección: entrada
    GPIO_SetDir(JOY_BTN_PORT, BIT(JOY_BTN_PIN), 0); // 0 = input
}

// =================== Configuración Timer0 (trigger ADC) ===================

static void cfgTimerForADC(void) {
    TIM_TIMERCFG_Type cfgTimer;
    TIM_MATCHCFG_Type cfgMatch01;

    // Timer base en us, prescale a 1000 -> tick = 1 ms
    cfgTimer.PrescaleOption = TIM_USVAL;
    cfgTimer.PrescaleValue  = 1000;
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &cfgTimer);

    // Match canal 1 (MAT0.1) para disparar el ADC periódicamente
    cfgMatch01.MatchChannel = TIM_MATCH_1;
    cfgMatch01.IntOnMatch   = DISABLE;             // no interrumpimos por Timer
    cfgMatch01.ResetOnMatch = ENABLE;
    cfgMatch01.StopOnMatch  = DISABLE;
    cfgMatch01.ExtMatchOutputType = TIM_EXTMATCH_TOGGLE;
    cfgMatch01.MatchValue   = 124;                 // ~124 ms (ajustable)

    TIM_ConfigMatch(LPC_TIM0, &cfgMatch01);

    // Habilitar Timer0
    TIM_Cmd(LPC_TIM0, ENABLE);
}

// =================== Configuración del ADC ===================

static void cfgADC(void) {
    // Inicializar ADC con tu rate
    ADC_Init(ADC_RATE);

    // Pines analógicos para CH0 y CH1
    ADC_PinConfig(ADC_CHANNEL_0);
    ADC_PinConfig(ADC_CHANNEL_1);

    // Sin burst, disparo por externa (Timer0 MAT0.1)
    ADC_BurstCmd(DISABLE);
    ADC_StartCmd(ADC_START_ON_MAT01);
    ADC_EdgeStartConfig(ADC_START_ON_FALLING);

    // Habilitamos CH0 inicialmente, CH1 deshabilitado
    ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);
    ADC_ChannelCmd(ADC_CHANNEL_1, DISABLE);

    // Habilitar interrupción global de ADC y de canales 0 y 1
    ADC_IntConfig(ADC_CHANNEL_0, ENABLE);
    ADC_IntConfig(ADC_CHANNEL_1, ENABLE);

    NVIC_EnableIRQ(ADC_IRQn);
}

// =================== Handler del ADC ===================

void ADC_IRQHandler(void) {
    // Incrementamos un "tiempo" artificial cada vez que entramos
    g_fakeTime += 10u;  // valor arbitrario; solo debe ser creciente

    if (ADC_GlobalGetStatus(ADC_DATA_DONE)) {
        // Secuencia:
        //  - Si CH0 listo, leemos VRx, habilitamos CH1 para la próxima conversión
        //  - Si CH1 listo, leemos VRy, habilitamos CH0 para la próxima

        if (ADC_ChannelGetStatus(ADC_CHANNEL_0, ADC_DATA_DONE)) {
            // Pasamos a CH1 para la próxima conversión
            ADC_ChannelCmd(ADC_CHANNEL_0, DISABLE);
            ADC_ChannelCmd(ADC_CHANNEL_1, ENABLE);

            vrx = ADC_ChannelGetData(ADC_CHANNEL_0);

            // ===== Movimiento horizontal con VRx =====
            if (vrx > UPPER_LIM) {
                // Joystick hacia la derecha
                BS_Placement_MoveCursor(BS_DIR_RIGHT);
            } else if (vrx < LOWER_LIM) {
                // Joystick hacia la izquierda
                BS_Placement_MoveCursor(BS_DIR_LEFT);
            }

        } else if (ADC_ChannelGetStatus(ADC_CHANNEL_1, ADC_DATA_DONE)) {
            // Pasamos de nuevo a CH0
            ADC_ChannelCmd(ADC_CHANNEL_1, DISABLE);
            ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);

            vry = ADC_ChannelGetData(ADC_CHANNEL_1);

            // ===== Movimiento vertical con VRy =====
            if (vry > UPPER_LIM) {
                // Joystick hacia arriba
                BS_Placement_MoveCursor(BS_DIR_UP);
            } else if (vry < LOWER_LIM) {
                // Joystick hacia abajo
                BS_Placement_MoveCursor(BS_DIR_DOWN);
            }
        }
    }

    // ================== Lectura del botón del joystick ==================
    {
        uint8_t btnNow = (GPIO_ReadValue(JOY_BTN_PORT) & BIT(JOY_BTN_PIN)) ? 1u : 0u;

        // Flanco de bajada: 1 -> 0  (botón presionado)
        if (btnNow == 0u && joyBtnLast == 1u) {
            // Intentar colocar el barco actual (2,4,6) en la posición del cursor
            BS_PlaceResult res = BS_Placement_TryPlaceCurrentShip(g_fakeTime);

            // Por ahora no hacemos nada especial con res,
            // pero podrías chequear BS_PLACE_ALL_DONE para saber
            // cuando ya pusiste 2,4 y 6 y pasar a modo de disparo más adelante.
            (void)res;
        }
        joyBtnLast = btnNow;
    }

    // Nota: la librería battleship_max se encarga internamente de
    // redibujar:
    //  - Bloque 0: preview del barco/cursor
    //  - Bloque 1: tablero del jugador
    //  - Bloque 2: tablero del contrincante (cuando toque)
    // cada vez que llamamos BS_Placement_MoveCursor o BS_Placement_TryPlaceCurrentShip.
}
